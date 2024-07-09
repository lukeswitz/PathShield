#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include "MacPrefixes.h"
#include <vector>
#include <algorithm>
#include <set>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
#define THRESHOLD_COUNT 5
#define SCAN_INTERVAL 5  // In seconds
#define MAX_DEVICES 75
#define DETECTION_WINDOW 300    // Adjustable: 1 minute (60), 5 minutes (300), 10 minutes (600)
#define STABILITY_THRESHOLD 10  // Higher threshold to reduce false positives
#define VARIATION_THRESHOLD 25  // Minimum variation in RSSI to flag a device as a potential tracker

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
  int stableRssiCount;  // Counter for stable RSSI values
  int variationCount;   // Counter for significant RSSI variations
};

DeviceInfo trackedDevices[MAX_DEVICES];
int deviceIndex = 0;
BLEScan *pBLEScan;
bool paused = false;
bool filterByName = false;  // New flag to filter by name
std::set<String> previouslyDetectedDevices;
const unsigned long scanInterval = SCAN_INTERVAL * 1000;
unsigned long lastScanTime = 0;
unsigned long lastButtonCheck = 0;

const char *specialMacs[] = {
  "00:25:DF", "20:3A:07", "34:DE:1A", "44:65:0D", "58:82:A8"
};

// Define the MyAdvertisedDeviceCallbacks class
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
  }
};

void setup() {
  M5.begin();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(3);  // Set to landscape mode (240x135)
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setTextSize(1);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1100);  // Optimize scan interval
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  // Initialize WiFi for scanning
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
}

void loop() {
  M5.update();

  // Debounce button presses
  unsigned long currentMillis = millis();
  if (currentMillis - lastButtonCheck >= 200) {
    lastButtonCheck = currentMillis;

    // Handle Button A press to toggle pause
    if (M5.BtnA.wasPressed()) {
      paused = !paused;
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextSize(3);
      if (paused) {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.setCursor(60, 20);
        M5.Lcd.print("Paused");
        for (int i = 0; i < 5; i++) {
          M5.Lcd.drawCircle(120, 70, 10 + i * 3, M5.Lcd.color565(255 - i * 40, i * 40, 0));
          delay(50);
        }
      } else {
        M5.Lcd.setTextColor(GREEN);
        M5.Lcd.setCursor(60, 20);
        M5.Lcd.print("Resumed");
        for (int i = 0; i < 5; i++) {
          M5.Lcd.drawCircle(120, 70, 10 + i * 3, M5.Lcd.color565(0, 255 - i * 40, i * 40));
          delay(50);
        }
      }
      M5.Lcd.setTextSize(1);
    } else if (M5.BtnB.wasPressed()) {
      filterByName = !filterByName;
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextSize(3);
      M5.Lcd.setCursor(1, 1);
      if (filterByName) {
        M5.Lcd.setTextColor(BLUE);
        M5.Lcd.print("Filter: ON");
        for (int i = 0; i < 5; i++) {
          M5.Lcd.drawTriangle(120, 60 + i * 5, 110, 80 + i * 5, 130, 80 + i * 5, M5.Lcd.color565(0, 0, 255 - i * 50));
          delay(50);
        }
      } else {
        M5.Lcd.setTextColor(ORANGE);
        M5.Lcd.print("Filter: OFF");
        for (int i = 0; i < 5; i++) {
          M5.Lcd.drawTriangle(120, 60 + i * 5, 110, 80 + i * 5, 130, 80 + i * 5, M5.Lcd.color565(255 - i * 50, 165 - i * 30, 0));
          delay(50);
        }
      }
    }
  }

  if (paused) {
    delay(100);  // Small delay to avoid busy-waiting
    return;
  }

  // Check scan interval
  if (currentMillis - lastScanTime >= scanInterval) {
    lastScanTime = currentMillis;

    // BLE Scanning
    BLEScanResults foundDevices = pBLEScan->start(5, false);  // Reduce scan duration to 5 seconds
    unsigned long currentTime = millis() / 1000;              // Current time in seconds
    bool newTrackerFound = false;                             // Flag to track new tracker detections

    for (int i = 0; i < foundDevices.getCount(); i++) {
      BLEAdvertisedDevice device = foundDevices.getDevice(i);
      if (trackDevice(device.getAddress().toString().c_str(), device.getRSSI(), currentTime, device.getName().c_str())) {
        newTrackerFound = true;  // Set flag if a new tracker is detected
      }
    }

    // WiFi Scanning
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
      String macAddr = WiFi.BSSIDstr(i);
      int rssi = WiFi.RSSI(i);
      if (trackDevice(macAddr.c_str(), rssi, currentTime, WiFi.SSID(i).c_str())) {
        newTrackerFound = true;  // Set flag if a new tracker is detected
      }
    }

    if (newTrackerFound) {
      alertUser();  // Trigger alert only if there is a new tracker detected
    }

    pBLEScan->clearResults();  // Delete results from BLEScan buffer to release memory
    displayTrackedDevices();
    removeOldEntries(currentTime);
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
        trackedDevices[i].stableRssiCount = 0;  // Reset if RSSI varies
      }
      if (rssiDifference >= VARIATION_THRESHOLD) {
        trackedDevices[i].variationCount++;
      }
      trackedDevices[i].lastRssi = rssi;
      if (String(name).length() > 0) {
        trackedDevices[i].name = String(name);  // Ensure name is updated if not empty
      }
      trackedDevices[i].manufacturer = getManufacturer(address);  // Update manufacturer
      found = true;
      if (isSpecialMac(address)) {
        trackedDevices[i].detected = true;
        trackedDevices[i].isSpecial = true;
        moveToTop(i);       // Move detected special device to top
        newTracker = true;  // Mark as a new tracker detection
      } else if (trackedDevices[i].count >= THRESHOLD_COUNT && trackedDevices[i].variationCount > THRESHOLD_COUNT) {
        trackedDevices[i].detected = true;
        trackedDevices[i].isSpecial = false;
        moveToTop(i);       // Move detected device to top
        newTracker = true;  // Mark as a new tracker detection
      }
      break;
    }
  }

  if (!found) {
    if (deviceIndex < MAX_DEVICES) {  // Ensure we don't exceed the array limit
      for (int j = deviceIndex; j > 0; j--) {
        trackedDevices[j] = trackedDevices[j - 1];
      }
      trackedDevices[0] = { String(address), String(name), getManufacturer(address), 1, currentTime, rssi, 1, rssi, false, false, 0, 0 };
      deviceIndex++;
      if (isSpecialMac(address) || (trackedDevices[0].count >= THRESHOLD_COUNT && trackedDevices[0].variationCount > THRESHOLD_COUNT)) {
        newTracker = true;  // Mark as a new tracker detection
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

void alertUser() {
  M5.Lcd.fillScreen(RED);
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.print("New Tracker Detected!");
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

  std::sort(detectedDevices.begin(), detectedDevices.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
    return b.count < a.count;
  });

  std::sort(otherDevices.begin(), otherDevices.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
    return b.count < a.count;
  });

  int y = 20;
  M5.Lcd.setTextSize(1);

  for (const auto &device : detectedDevices) {
    if (filterByName && device.name.length() == 0) {
      continue;  // Skip devices without a name if filter is enabled
    }
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(1, y);
    M5.Lcd.setTextColor(RED);
    M5.Lcd.print(device.name);
    M5.Lcd.print(" ");
    M5.Lcd.print(device.address);
    y += 15;

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(1, y);
    M5.Lcd.print(device.manufacturer);
    M5.Lcd.print("  Count: ");
    M5.Lcd.print(device.count);
    y += 10;

    if (y >= SCREEN_HEIGHT - 20) {
      return;  // Stop if we reach the bottom of the screen
    }
  }

  // Display other devices
  for (const auto &device : otherDevices) {
    if (filterByName && device.name.length() == 0) {
      continue;  // Skip devices without a name if filter is enabled
    }
    M5.Lcd.setCursor(1, y);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.print(device.name);
    M5.Lcd.print(" ");
    M5.Lcd.print(device.address);
    y += 10;

    M5.Lcd.setCursor(1, y);
    M5.Lcd.print(device.manufacturer);
    M5.Lcd.print("  Count: ");
    M5.Lcd.print(device.count);
    y += 15;

    if (y >= SCREEN_HEIGHT - 20) {
      return;  // Stop if we reach the bottom of the screen
    }
  }
}

void removeOldEntries(unsigned long currentTime) {
  for (int i = 0; i < deviceIndex; i++) {
    if (currentTime - trackedDevices[i].lastSeen > DETECTION_WINDOW) {
      // Shift the remaining elements to the left
      for (int j = i; j < deviceIndex - 1; j++) {
        trackedDevices[j] = trackedDevices[j + 1];
      }
      deviceIndex--;  // Decrement the device count
      i--;            // Recheck the new element at this index
    }
  }
}
