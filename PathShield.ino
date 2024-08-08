#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "MacPrefixes.h"
#include <vector>
#include <algorithm>
#include <set>
#include <SPIFFS.h>
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
#define THRESHOLD_COUNT 5
#define SCAN_INTERVAL 10  // In seconds
#define MAX_DEVICES 100
#define DETECTION_WINDOW 300
#define STABILITY_THRESHOLD 10
#define VARIATION_THRESHOLD 20
#define IDLE_TIMEOUT 30000  // 30 seconds in milliseconds
#define SENSITIVITY 5  // Example sensitivity value

const char *ssid = "your_SSID";
const char *password = "your_PASSWORD";
const char *nzyme_host = "your_nzyme_host";
const int nzyme_port = 443;
const char *nzyme_path = "/api/your_endpoint";  // Adjust the path as per Nzyme docs
bool highBrightness = true;
bool didTrigger = false;

WiFiClientSecure client;

struct DeviceInfo {
  String address;
  String name;
  String manufacturer;
  int count;
  unsigned long lastSeen;
  int rssiSum;
  int rssiCount;
  int lastRssi;
  bool detected;
  bool isSpecial;
  bool alertTriggered;  // Add this flag
  int stableRssiCount;
  int variationCount;
  int lastSeenTime;  // Add this line
};


DeviceInfo trackedDevices[MAX_DEVICES];
unsigned long currentMillis;
int deviceIndex = 0;
BLEScan *pBLEScan;
bool paused = false;
bool filterByName = false;  // New flag to filter by name
std::set<String> previouslyDetectedDevices;
const unsigned long scanInterval = SCAN_INTERVAL * 1000;
unsigned long lastScanTime = 0;
unsigned long lastButtonCheck = 0;
unsigned long lastButtonPressTime = 0;
bool screenDimmed = false;

const char *specialMacs[] = {
  "00:25:DF", "20:3A:07", "34:DE:1A", "44:65:0D", "58:82:A8"
};

int scrollIndex = 0;  // Add this line to keep track of the scroll position
const int devicesPerPage = 10;  // Add this line to define the number of devices displayed per screen

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    //Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
  }
};

void setup() {
  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  M5.begin();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(3);  // Set to landscape mode (240x135)
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setTextSize(1);
  M5.Axp.ScreenBreath(80);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1100);  // Optimize scan interval
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  loadDeviceData();
}

void loop() {
  M5.update();
  currentMillis = millis();

  // Handle BtnA press
  if (M5.BtnA.wasPressed()) {
    handleBtnA();
  }

  // Handle BtnB press
  if (M5.BtnB.wasPressed()) {
    handleBtnB();
  }

  // Handle button combination for menu access
  handleButtonCombination();

  if (paused) {
    delay(100);  // Small delay to avoid busy-waiting
    return;
  }

  // Dimming the screen after 30 seconds of inactivity
  if (currentMillis - lastButtonPressTime > IDLE_TIMEOUT && highBrightness) {
    highBrightness = false;
    M5.Axp.ScreenBreath(30);
    screenDimmed = true;
  }

  // BT Scanning
  BLEScanResults foundDevices = pBLEScan->start(5, false);
  unsigned long currentTime = millis() / 1000;
  bool newTrackerFound = false;

  for (int i = 0; i < foundDevices.getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    if (trackDevice(device.getAddress().toString().c_str(), device.getRSSI(), currentTime, device.getName().c_str())) {
      newTrackerFound = true;
    }
  }

  if (newTrackerFound) {
    // Get the name and address of the last detected device
    String lastName = trackedDevices[0].name;
    String lastAddress = trackedDevices[0].address;
    bool isSpecial = trackedDevices[0].isSpecial;
    alertUser(isSpecial, lastName, lastAddress);  // Pass the correct parameters to alertUser
  }

  pBLEScan->clearResults();
  displayTrackedDevices();
  removeOldEntries(currentTime);

  // Send data to Nzyme periodically
  static unsigned long lastNzymeSendTime = 0;
  if (currentMillis - lastNzymeSendTime > scanInterval) {
    lastNzymeSendTime = currentMillis;
    sendToNzyme(prepareNzymePayload());
  }

  saveDeviceData();  // Save device data persistently
}

void handleBtnA() {
  lastButtonPressTime = millis();  // Reset the timer for screen dimming
  highBrightness = true;
  M5.Axp.ScreenBreath(80);
  screenDimmed = false;

  if (paused) {
    // Scroll up
    if (scrollIndex > 0) {
      scrollIndex--;
    }
  } else {
    paused = !paused;
  }

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(3);
  if (paused) {
    M5.Lcd.setTextColor(RED);
    M5.Lcd.setCursor(60, 20);
    M5.Lcd.print("Paused");
  } else {
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setCursor(60, 20);
    M5.Lcd.print("Resumed");
  }
  M5.Lcd.setTextSize(1);
  displayTrackedDevices();
}

void handleBtnB() {
  lastButtonPressTime = millis();  // Reset the timer for screen dimming
  highBrightness = true;
  M5.Axp.ScreenBreath(80);
  screenDimmed = false;

  if (paused) {
    // Scroll down
    if (scrollIndex < (deviceIndex - devicesPerPage)) {
      scrollIndex++;
    }
  } else {
    filterByName = !filterByName;
  }
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(2, 1);
  if (filterByName) {
    M5.Lcd.setTextColor(BLUE);
    M5.Lcd.print("Filter: ON");
  } else {
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.print("Filter: OFF");
  }
  M5.Lcd.setTextSize(1);
  displayTrackedDevices();
}

void handleButtonCombination() {
  static bool inMenu = false;
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    if (inMenu) {
      M5.Lcd.fillScreen(BLACK);
      displayTrackedDevices();
      inMenu = false;
    } else {
      displayMenuScreen();
      inMenu = true;
    }
  }
}

bool isSpecialMac(const char *address) {
  for (int i = 0; i < sizeof(specialMacs) / sizeof(specialMacs[0]); i++) {
    if (String(address).startsWith(specialMacs[i])) {
      return true;
    }
  }
  return false;
}

bool trackDevice(const char *address, int rssi, unsigned long currentTime, const char *name) {
  bool found = false;
  bool newTracker = false;

  for (int i = 0; i < deviceIndex; i++) {
    if (trackedDevices[i].address.equals(address)) {
      trackedDevices[i].count++;
      trackedDevices[i].lastSeen = currentTime;
      trackedDevices[i].rssiSum += rssi;
      trackedDevices[i].rssiCount++;
      int rssiDifference = abs(trackedDevices[i].lastRssi - rssi);
      if (rssiDifference <= STABILITY_THRESHOLD) {
        trackedDevices[i].stableRssiCount++;
      } else {
        trackedDevices[i].stableRssiCount = 0;
      }
      if (rssiDifference >= VARIATION_THRESHOLD) {
        trackedDevices[i].variationCount++;
      }
      trackedDevices[i].lastRssi = rssi;
      if (String(name).length() > 0) {
        trackedDevices[i].name = String(name);
      }
      trackedDevices[i].manufacturer = getManufacturer(address);
      found = true;
      if (isSpecialMac(address)) {
        trackedDevices[i].detected = true;
        trackedDevices[i].isSpecial = true;
        if (!trackedDevices[i].alertTriggered) {
          trackedDevices[i].alertTriggered = true;  // Trigger alert only once
          newTracker = true;
        }
        moveToTop(i);
      } else if (trackedDevices[i].count >= THRESHOLD_COUNT && trackedDevices[i].variationCount > THRESHOLD_COUNT) {
        trackedDevices[i].detected = true;
        trackedDevices[i].isSpecial = false;
        if (!trackedDevices[i].alertTriggered) {
          trackedDevices[i].alertTriggered = true;  // Trigger alert only once
          newTracker = true;
        }
        moveToTop(i);
      }
      break;
    }
  }

  if (!found) {
    if (deviceIndex < MAX_DEVICES) {
      for (int j = deviceIndex; j > 0; j--) {
        trackedDevices[j] = trackedDevices[j - 1];
      }
      trackedDevices[0] = { String(address), String(name), getManufacturer(address), 1, currentTime, rssi, 1, rssi, false, false, false, 0, 0, currentTime };
      deviceIndex++;
      if (isSpecialMac(address) || (trackedDevices[0].count >= THRESHOLD_COUNT && trackedDevices[0].variationCount > THRESHOLD_COUNT)) {
        trackedDevices[0].alertTriggered = true;  // Trigger alert only once
        newTracker = true;
      }
      previouslyDetectedDevices.insert(trackedDevices[0].address);
    }
  }

  return newTracker;
}

void moveToTop(int index) {
  if (index <= 0) return;
  DeviceInfo temp = trackedDevices[index];
  for (int i = index; i > 0; i--) {
    trackedDevices[i] = trackedDevices[i - 1];
  }
  trackedDevices[0] = temp;
}


void alertUser(bool isSpecial, const String &name, const String &mac) {
  didTrigger = !didTrigger;
  if (isSpecial) {  // flash red/blue five times
    for (int i = 0; i < 5; i++) {
      M5.Lcd.fillScreen(RED);
      delay(200);
      M5.Lcd.fillScreen(BLUE);
      delay(200);
    }
  } else {
    M5.Lcd.fillScreen(RED);
  }

  M5.Lcd.setCursor(0, 10);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.print("New Tracker Detected!");
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.print("Name: ");
  M5.Lcd.print(name);
  M5.Lcd.setCursor(0, 60);
  M5.Lcd.print("MAC: ");
  M5.Lcd.print(mac);
  delay(1000);  // Display alert for 1 second
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
}

void displayTrackedDevices() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(5, 1);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setTextSize(2);
  M5.Lcd.print("PathShield: Devices");
  M5.Lcd.setTextSize(1);

  std::vector<DeviceInfo> detectedDevices;
  std::vector<DeviceInfo> otherDevices;

  for (int i = 0; i < deviceIndex; i++) {
    if (trackedDevices[i].detected) {
      detectedDevices.push_back(trackedDevices[i]);
    } else {
      otherDevices.push_back(trackedDevices[i]);
    }
  }

  // Sort detected devices by count in descending order
  std::sort(detectedDevices.begin(), detectedDevices.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
    return b.count < a.count;
  });

  // Sort other devices by count in descending order
  std::sort(otherDevices.begin(), otherDevices.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
    return b.count < a.count;
  });

  int y = 20;
  M5.Lcd.setTextSize(1);

  // Combine detected and other devices for display
  std::vector<DeviceInfo> allDevices = detectedDevices;
  allDevices.insert(allDevices.end(), otherDevices.begin(), otherDevices.end());

  // Display devices based on scrollIndex and devicesPerPage
  for (int i = scrollIndex; i < scrollIndex + devicesPerPage && i < allDevices.size(); i++) {
    const auto &device = allDevices[i];
    if (device.isSpecial) {
      M5.Lcd.setTextColor(BLUE);
    } else if (device.detected) {
      M5.Lcd.setTextColor(RED);
    } else {
      M5.Lcd.setTextColor(WHITE);
    }
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print(device.name);
    M5.Lcd.print(" ");
    M5.Lcd.print(device.address);
    y += 10;

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print(device.manufacturer);
    M5.Lcd.print("  Count: ");
    M5.Lcd.print(device.count);
    y += 12;

    if (y >= SCREEN_HEIGHT - 20) {
      return;  // Stop if we reach the bottom of the screen
    }
  }
}

void displayMenuScreen() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(5, 1);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setTextSize(2);
    M5.Lcd.print("Menu");

    M5.Lcd.setTextSize(1);
    int y = 20;

    // Display battery level
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print("Battery: ");
    M5.Lcd.print(M5.Axp.GetBatVoltage());
    M5.Lcd.print("V");
    y += 12;

    // Display brightness level
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print("Brightness: ");
    M5.Lcd.print(highBrightness ? "High" : "Low");
    y += 12;

    // Display number of devices found
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print("Devices Found: ");
    M5.Lcd.print(deviceIndex);
    y += 12;

    // Display sensitivity setting
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print("Sensitivity: ");
    M5.Lcd.print(SENSITIVITY);  // Assuming SENSITIVITY is a defined constant
    y += 12;

    // Display other settings
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print("Other Settings: ");
    y += 12;

    // Add more settings as needed

    // Add a "Back" option
    M5.Lcd.setCursor(2, y);
    M5.Lcd.print("Press BtnA + BtnB to go back");
}

void removeOldEntries(unsigned long currentTime) {
  for (int i = 0; i < deviceIndex; i++) {
    if (currentTime - trackedDevices[i].lastSeen > DETECTION_WINDOW) {
      for (int j = i; j < deviceIndex - 1; j++) {
        trackedDevices[j] = trackedDevices[j + 1];
      }
      deviceIndex--;
      i--;
    }
  }
}

String prepareNzymePayload() {
  String payload = "{\"devices\":[";
  for (int i = 0; i < deviceIndex; i++) {
    if (i > 0) payload += ",";
    payload += "{";
    payload += "\"address\":\"" + trackedDevices[i].address + "\",";
    payload += "\"name\":\"" + trackedDevices[i].name + "\",";
    payload += "\"manufacturer\":\"" + trackedDevices[i].manufacturer + "\",";
    payload += "\"count\":" + String(trackedDevices[i].count) + ",";
    payload += "\"lastSeen\":" + String(trackedDevices[i].lastSeen) + ",";
    payload += "\"rssiSum\":" + String(trackedDevices[i].rssiSum) + ",";
    payload += "\"rssiCount\":" + String(trackedDevices[i].rssiCount) + ",";
    payload += "\"lastRssi\":" + String(trackedDevices[i].lastRssi) + ",";
    payload += "\"detected\":" + String(trackedDevices[i].detected) + ",";
    payload += "\"isSpecial\":" + String(trackedDevices[i].isSpecial) + ",";
    payload += "\"stableRssiCount\":" + String(trackedDevices[i].stableRssiCount) + ",";
    payload += "\"variationCount\":" + String(trackedDevices[i].variationCount);
    payload += "}";
  }
  payload += "]}";
  return payload;
}

void sendToNzyme(const String &jsonPayload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  if (!client.connect(nzyme_host, nzyme_port)) {
    Serial.println("Connection to Nzyme failed!");
    return;
  }

  String request = String("POST ") + nzyme_path + " HTTP/1.1\r\n" + 
                   "Host: " + nzyme_host + "\r\n" + 
                   "Content-Type: application/json\r\n" + 
                   "Content-Length: " + jsonPayload.length() + "\r\n" + 
                   "Connection: close\r\n\r\n" + 
                   jsonPayload + "\r\n";

  client.print(request);

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break;
    }
  }
  while (client.available()) {
    String line = client.readStringUntil('\n');
    Serial.println(line);
  }

  client.stop();
}

void saveDeviceData() {
  File file = SPIFFS.open("/devices.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  for (int i = 0; i < deviceIndex; i++) {
    file.println(trackedDevices[i].address + "," + String(trackedDevices[i].lastSeen));
  }

  file.close();
}

void loadDeviceData() {
  File file = SPIFFS.open("/devices.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    int commaIndex = line.indexOf(',');
    String address = line.substring(0, commaIndex);
    unsigned long lastSeen = line.substring(commaIndex + 1).toInt();

    // Add the device to the trackedDevices array
    if (deviceIndex < MAX_DEVICES) {
      trackedDevices[deviceIndex] = { address, "", "", 0, lastSeen, 0, 0, 0, false, false, false, 0, 0, lastSeen };
      deviceIndex++;
    }
  }

  file.close();
}