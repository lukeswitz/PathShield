#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "MacPrefixes.h"
#include <vector>
#include <algorithm>
#include <set>
#include <SPIFFS.h>
#include <WiFi.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
#define MAX_DEVICES 100
#define DETECTION_WINDOW 300
#define IDLE_TIMEOUT 30000
#define SCAN_INTERVAL 5

#define BLUE_GREY 0x5D9B 

// Time windows for persistence analysis
#define WINDOW_RECENT 300
#define WINDOW_MEDIUM 600
#define WINDOW_OLD 900
#define WINDOW_OLDEST 1200 // 20 minutes

// Detection thresholds
#define MIN_DETECTIONS 8       // M-out-of-N logic: require 8 detections
#define MIN_WINDOWS 3          // Must appear in at least 3 time windows
#define PERSISTENCE_THRESHOLD 0.65  // Minimum persistence score
#define RSSI_STABILITY_THRESHOLD 10
#define RSSI_VARIATION_THRESHOLD 20
#define EPSILON_CONNECTED_GAP 180  // 3 minutes max gap for ε-connectedness

unsigned long lastBtnAPress = 0;
unsigned long lastBtnBPress = 0;
unsigned long lastComboCheck = 0;
const unsigned long DEBOUNCE_DELAY = 200;
bool inMenu = false;
int menuIndex = 0;
int menuBaseY = 0;

bool highBrightness = true;
bool paused = false;
bool filterByName = false;
bool screenDimmed = false;

struct WiFiDeviceInfo {
  String ssid;
  String bssid;
  int rssi;
  int channel;
  int encryptionType;
  unsigned long lastSeen;
  int detectionCount;
};

WiFiDeviceInfo wifiDevices[50];
int wifiDeviceIndex = 0;
bool scanningWiFi = true;
unsigned long lastScanSwitch = 0;
const unsigned long SCAN_SWITCH_INTERVAL = 3000;

const char *specialMacs[] = {
  "00:25:DF", "20:3A:07", "34:DE:1A", "44:65:0D", "58:82:A8"
};

struct TimeWindow {
  unsigned long start;
  unsigned long end;
  int detections;
};

struct DeviceInfo {
  String address;
  String name;
  String manufacturer;
  int totalCount;
  unsigned long firstSeen;
  unsigned long lastSeen;
  int rssiSum;
  int rssiCount;
  int lastRssi;
  bool detected;
  bool isSpecial;
  bool alertTriggered;
  int stableRssiCount;
  int variationCount;
  float persistenceScore;
  TimeWindow windows[4];  // recent, medium, old, oldest
  std::vector<unsigned long> detectionTimestamps;
};

DeviceInfo trackedDevices[MAX_DEVICES];
int deviceIndex = 0;
BLEScan *pBLEScan;
unsigned long lastScanTime = 0;
unsigned long lastButtonPressTime = 0;
int scrollIndex = 0;
std::set<String> ignoreList;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
  }
};

void trackWiFiDevice(String ssid, String bssid, int rssi, int channel, int encType, unsigned long currentTime) {
  bool found = false;
  
  for (int i = 0; i < wifiDeviceIndex; i++) {
    if (wifiDevices[i].bssid.equals(bssid)) {
      wifiDevices[i].rssi = rssi;
      wifiDevices[i].lastSeen = currentTime;
      wifiDevices[i].detectionCount++;
      found = true;
      break;
    }
  }
  
  if (!found && wifiDeviceIndex < 50) {
    wifiDevices[wifiDeviceIndex].ssid = ssid;
    wifiDevices[wifiDeviceIndex].bssid = bssid;
    wifiDevices[wifiDeviceIndex].rssi = rssi;
    wifiDevices[wifiDeviceIndex].channel = channel;
    wifiDevices[wifiDeviceIndex].encryptionType = encType;
    wifiDevices[wifiDeviceIndex].lastSeen = currentTime;
    wifiDevices[wifiDeviceIndex].detectionCount = 1;
    wifiDeviceIndex++;
  }
}

void removeOldWiFiEntries(unsigned long currentTime) {
  for (int i = 0; i < wifiDeviceIndex; i++) {
    if (currentTime - wifiDevices[i].lastSeen > DETECTION_WINDOW) {
      for (int j = i; j < wifiDeviceIndex - 1; j++) {
        wifiDevices[j] = wifiDevices[j + 1];
      }
      wifiDeviceIndex--;
      i--;
    }
  }
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(3);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setTextSize(1);
  M5.Axp.ScreenBreath(80);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1100);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }

  loadIgnoreList();
  loadDeviceData();
  
  displayStartupMessage();
  delay(2000);
}

void loop() {
  M5.update();
  unsigned long currentMillis = millis();

  if (checkButtonCombo()) {
    handleButtonCombination();
    return;
  }

  if (!inMenu) {
    if (M5.BtnA.wasPressed() && (currentMillis - lastBtnAPress > DEBOUNCE_DELAY)) {
      lastBtnAPress = currentMillis;
      handleBtnA();
      return;
    }

    if (M5.BtnB.wasPressed() && (currentMillis - lastBtnBPress > DEBOUNCE_DELAY)) {
      lastBtnBPress = currentMillis;
      handleBtnB();
      return;
    }
  }

  if (paused) {
    delay(100);
    return;
  }

  if (currentMillis - lastButtonPressTime > IDLE_TIMEOUT && highBrightness) {
    highBrightness = false;
    M5.Axp.ScreenBreath(30);
    screenDimmed = true;
  }

  // Alternate between WiFi and BLE scanning
  if (currentMillis - lastScanSwitch > SCAN_SWITCH_INTERVAL) {
    scanningWiFi = !scanningWiFi;
    lastScanSwitch = currentMillis;
  }

  unsigned long currentTime = millis() / 1000;
  
  if (scanningWiFi) {
    // WiFi scan
    int n = WiFi.scanNetworks(false, false, false, 300);
    for (int i = 0; i < n; i++) {
      trackWiFiDevice(
        WiFi.SSID(i),
        WiFi.BSSIDstr(i),
        WiFi.RSSI(i),
        WiFi.channel(i),
        WiFi.encryptionType(i),
        currentTime
      );
    }
    WiFi.scanDelete();
  } else {
    // BLE scan
    BLEScanResults *foundDevicesPtr = pBLEScan->start(3, false);
    if (foundDevicesPtr) {
      bool newTrackerFound = false;
      
      for (int i = 0; i < foundDevicesPtr->getCount(); i++) {
        BLEAdvertisedDevice device = foundDevicesPtr->getDevice(i);
        String macAddr = device.getAddress().toString().c_str();
        
        if (ignoreList.find(macAddr) == ignoreList.end()) {
          if (trackDevice(macAddr.c_str(), device.getRSSI(), currentTime, device.getName().c_str())) {
            newTrackerFound = true;
          }
        }
      }

      if (newTrackerFound) {
        String lastName = trackedDevices[0].name;
        String lastAddress = trackedDevices[0].address;
        bool isSpecial = trackedDevices[0].isSpecial;
        float persistence = trackedDevices[0].persistenceScore;
        alertUser(isSpecial, lastName, lastAddress, persistence);
      }
      
      pBLEScan->clearResults();
    }
  }

  displayTrackedDevices();
  removeOldEntries(currentTime);
  removeOldWiFiEntries(currentTime);
  
  static unsigned long lastSaveTime = 0;
  if (currentMillis - lastSaveTime > 60000) {
    saveDeviceData();
    lastSaveTime = currentMillis;
  }
}

void displayStartupMessage() {
  M5.Lcd.fillScreen(BLACK);
  
  for (int i = 0; i < 20; i++) {
    int x = random(0, SCREEN_WIDTH);
    int y = random(0, SCREEN_HEIGHT);
    int w = random(5, 40);
    M5.Lcd.drawFastHLine(x, y, w, random(0x0000, 0x1111));
  }
  
  M5.Lcd.drawFastHLine(0, 20, SCREEN_WIDTH, CYAN);
  M5.Lcd.drawFastHLine(0, 21, SCREEN_WIDTH, CYAN);
  
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.setCursor(15, 30);
  M5.Lcd.print("PATH");
  M5.Lcd.setTextColor(MAGENTA);
  M5.Lcd.print("SHIELD");
  
  M5.Lcd.setTextColor(0x07E0);
  M5.Lcd.setCursor(16, 31);
  M5.Lcd.print("PATH");
  
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.setCursor(50, 60);
  M5.Lcd.print("TRACKER DETECTION");
  
  M5.Lcd.setTextColor(DARKGREY);
  M5.Lcd.setCursor(85, 72);
  M5.Lcd.print("v2.0");
  
  M5.Lcd.drawFastHLine(0, 85, SCREEN_WIDTH, MAGENTA);
  
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(GREEN);
  
  M5.Lcd.setCursor(10, 95);
  M5.Lcd.print("> Initializing BLE");
  delay(400);
  
  M5.Lcd.setCursor(190, 95);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.print("[OK]");
  delay(300);
  
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setCursor(10, 107);
  M5.Lcd.print("> Loading SPIFFS");
  delay(400);
  
  M5.Lcd.setCursor(190, 107);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.print("[OK]");
  delay(300);
  
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setCursor(10, 119);
  M5.Lcd.print("> System Ready");
  delay(300);
  
  M5.Lcd.setCursor(190, 119);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.print("[OK]");
  
  delay(400);
  M5.Lcd.fillScreen(CYAN);
  delay(50);
  M5.Lcd.fillScreen(BLACK);
  delay(50);
}

bool checkButtonCombo() {
  unsigned long currentMillis = millis();
  
  // Check if both buttons pressed simultaneously
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    if (currentMillis - lastComboCheck > 300) { // 300ms debounce for combo
      lastComboCheck = currentMillis;
      return true;
    }
  }
  return false;
}

void handleBtnA() {
  lastButtonPressTime = millis();
  
  if (screenDimmed) {
    highBrightness = true;
    M5.Axp.ScreenBreath(80);
    screenDimmed = false;
    displayTrackedDevices();
    return;
  }

  if (paused) {
    // Scroll up when paused
    if (scrollIndex > 0) {
      scrollIndex--;
      displayTrackedDevices();
    }
  } else {
    // Toggle pause
    paused = true;
    scrollIndex = 0;
    
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(RED);
    M5.Lcd.setCursor(50, 50);
    M5.Lcd.print("PAUSED");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(20, 90);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.print("Hold B to Resume");
    delay(1500);
    displayTrackedDevices();
  }
}
void handleBtnB() {
  lastButtonPressTime = millis();
  
  if (screenDimmed) {
    highBrightness = true;
    M5.Axp.ScreenBreath(80);
    screenDimmed = false;
    displayTrackedDevices();
    return;
  }

  if (paused) {
    // Check if button is held for 1 second to resume
    unsigned long pressStart = millis();
    bool stillPressed = true;
    
    while (M5.BtnB.isPressed() && (millis() - pressStart < 1000)) {
      M5.update();
      delay(10);
    }
    
    if (millis() - pressStart >= 1000) {
      // Held for 1 second - RESUME
      paused = false;
      scrollIndex = 0;
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(GREEN);
      M5.Lcd.setCursor(30, 50);
      M5.Lcd.print("RESUMED");
      M5.Lcd.setTextSize(1);
      delay(800);
      displayTrackedDevices();
    } else if (M5.BtnB.isPressed() == false) {
      // Quick press - scroll down
      int maxScroll = max(0, deviceIndex - 4);
      if (scrollIndex < maxScroll) {
        scrollIndex++;
        displayTrackedDevices();
      }
    }
  } else {
    // Toggle filter
    filterByName = !filterByName;
    
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(30, 50);
    if (filterByName) {
      M5.Lcd.setTextColor(BLUE);
      M5.Lcd.print("Named Only");
    } else {
      M5.Lcd.setTextColor(ORANGE);
      M5.Lcd.print("Show All");
    }
    M5.Lcd.setTextSize(1);
    delay(800);
    displayTrackedDevices();
  }
}

void handleButtonCombination() {
  inMenu = !inMenu;
  
  if (inMenu) {
    menuIndex = 0;
    displayMenuScreen();
    highlightMenuOption(menuIndex);
    
    // Wait for buttons to be released
    while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
      M5.update();
      delay(10);
    }
    
    // Menu navigation loop
    while (inMenu) {
      M5.update();
      delay(50);
      
      // Check for exit combo
      if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
        delay(200);
        while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
          M5.update();
          delay(10);
        }
        inMenu = false;
        M5.Lcd.fillScreen(BLACK);
        displayTrackedDevices();
        break;
      }
      
      // Navigate menu with BtnA
      if (M5.BtnA.wasPressed()) {
        menuIndex = (menuIndex + 1) % 3;
        displayMenuScreen();
        highlightMenuOption(menuIndex);
        delay(200);
      }
      
      // Select with BtnB
      if (M5.BtnB.wasPressed()) {
        executeMenuOption(menuIndex);
        delay(200);
      }
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

float calculatePersistenceScore(const DeviceInfo &device, unsigned long currentTime) {
  float score = 0.0;
  int factors = 0;

  // Factor 1: Detection frequency (0-0.25)
  if (device.totalCount >= MIN_DETECTIONS) {
    score += 0.25 * (float)min(device.totalCount, 30) / 30.0;
    factors++;
  }

  // Factor 2: Time window distribution (0-0.30)
  int windowsActive = 0;
  for (int i = 0; i < 4; i++) {
    if (device.windows[i].detections > 0) {
      windowsActive++;
    }
  }
  if (windowsActive >= MIN_WINDOWS) {
    score += 0.30 * (float)windowsActive / 4.0;
    factors++;
  }

  // Factor 3: ε-connectedness (0-0.25)
  bool isConnected = true;
  for (size_t i = 1; i < device.detectionTimestamps.size(); i++) {
    if (device.detectionTimestamps[i] - device.detectionTimestamps[i-1] > EPSILON_CONNECTED_GAP) {
      isConnected = false;
      break;
    }
  }
  if (isConnected && device.detectionTimestamps.size() >= 3) {
    score += 0.25;
    factors++;
  }

  // Factor 4: RSSI pattern (0-0.20)
  if (device.stableRssiCount > 5 || device.variationCount > 5) {
    score += 0.20 * (float)min(device.variationCount, 10) / 10.0;
    factors++;
  }

  return (factors > 0) ? score : 0.0;
}

void updateTimeWindows(DeviceInfo &device, unsigned long currentTime) {
  // Window 0: Recent (0-5 min)
  device.windows[0].start = currentTime >= WINDOW_RECENT ? currentTime - WINDOW_RECENT : 0;
  device.windows[0].end = currentTime;
  
  // Window 1: Medium (5-10 min)
  device.windows[1].start = currentTime >= WINDOW_MEDIUM ? currentTime - WINDOW_MEDIUM : 0;
  device.windows[1].end = currentTime >= WINDOW_RECENT ? currentTime - WINDOW_RECENT : 0;
  
  // Window 2: Old (10-15 min)
  device.windows[2].start = currentTime >= WINDOW_OLD ? currentTime - WINDOW_OLD : 0;
  device.windows[2].end = currentTime >= WINDOW_MEDIUM ? currentTime - WINDOW_MEDIUM : 0;
  
  // Window 3: Oldest (15-20 min)
  device.windows[3].start = currentTime >= WINDOW_OLDEST ? currentTime - WINDOW_OLDEST : 0;
  device.windows[3].end = currentTime >= WINDOW_OLD ? currentTime - WINDOW_OLD : 0;

  // Count detections in each window
  for (int w = 0; w < 4; w++) {
    device.windows[w].detections = 0;
    for (size_t i = 0; i < device.detectionTimestamps.size(); i++) {
      if (device.detectionTimestamps[i] >= device.windows[w].start && 
          device.detectionTimestamps[i] < device.windows[w].end) {
        device.windows[w].detections++;
      }
    }
  }
}

bool trackDevice(const char *address, int rssi, unsigned long currentTime, const char *name) {
  bool found = false;
  bool newTracker = false;

  for (int i = 0; i < deviceIndex; i++) {
    if (trackedDevices[i].address.equals(address)) {
      trackedDevices[i].totalCount++;
      trackedDevices[i].lastSeen = currentTime;
      trackedDevices[i].rssiSum += rssi;
      trackedDevices[i].rssiCount++;
      trackedDevices[i].detectionTimestamps.push_back(currentTime);
      
      // Keep only recent timestamps (last 20 minutes)
      while (!trackedDevices[i].detectionTimestamps.empty() && 
             trackedDevices[i].detectionTimestamps.front() < currentTime - WINDOW_OLDEST) {
        trackedDevices[i].detectionTimestamps.erase(trackedDevices[i].detectionTimestamps.begin());
      }
      
      int rssiDifference = abs(trackedDevices[i].lastRssi - rssi);
      if (rssiDifference <= RSSI_STABILITY_THRESHOLD) {
        trackedDevices[i].stableRssiCount++;
      } else {
        trackedDevices[i].stableRssiCount = 0;
      }
      if (rssiDifference >= RSSI_VARIATION_THRESHOLD) {
        trackedDevices[i].variationCount++;
      }
      trackedDevices[i].lastRssi = rssi;
      
      if (String(name).length() > 0) {
        trackedDevices[i].name = String(name);
      }
      trackedDevices[i].manufacturer = getManufacturer(address);
      
      updateTimeWindows(trackedDevices[i], currentTime);
      trackedDevices[i].persistenceScore = calculatePersistenceScore(trackedDevices[i], currentTime);
      
      found = true;
      
      if (isSpecialMac(address)) {
        trackedDevices[i].detected = true;
        trackedDevices[i].isSpecial = true;
        if (!trackedDevices[i].alertTriggered) {
          trackedDevices[i].alertTriggered = true;
          newTracker = true;
        }
        moveToTop(i);
      } else if (trackedDevices[i].persistenceScore >= PERSISTENCE_THRESHOLD) {
        trackedDevices[i].detected = true;
        trackedDevices[i].isSpecial = false;
        if (!trackedDevices[i].alertTriggered) {
          trackedDevices[i].alertTriggered = true;
          newTracker = true;
        }
        moveToTop(i);
      }
      break;
    }
  }

  if (!found && deviceIndex < MAX_DEVICES) {
    for (int j = deviceIndex; j > 0; j--) {
      trackedDevices[j] = trackedDevices[j - 1];
    }
    
    DeviceInfo newDevice;
    newDevice.address = String(address);
    newDevice.name = String(name);
    newDevice.manufacturer = getManufacturer(address);
    newDevice.totalCount = 1;
    newDevice.firstSeen = currentTime;
    newDevice.lastSeen = currentTime;
    newDevice.rssiSum = rssi;
    newDevice.rssiCount = 1;
    newDevice.lastRssi = rssi;
    newDevice.detected = false;
    newDevice.isSpecial = false;
    newDevice.alertTriggered = false;
    newDevice.stableRssiCount = 0;
    newDevice.variationCount = 0;
    newDevice.persistenceScore = 0.0;
    newDevice.detectionTimestamps.push_back(currentTime);
    
    for (int w = 0; w < 4; w++) {
      newDevice.windows[w].start = 0;
      newDevice.windows[w].end = 0;
      newDevice.windows[w].detections = 0;
    }
    
    trackedDevices[0] = newDevice;
    deviceIndex++;
    
    if (isSpecialMac(address)) {
      trackedDevices[0].detected = true;
      trackedDevices[0].isSpecial = true;
      trackedDevices[0].alertTriggered = true;
      newTracker = true;
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

void alertUser(bool isSpecial, const String &name, const String &mac, float persistence) {
  if (isSpecial) {
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
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.print("Tracker Detected!");
  M5.Lcd.setTextSize(1);
  
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.print("Type: ");
  M5.Lcd.print(isSpecial ? "KNOWN" : "SUSPECTED");
  
  M5.Lcd.setCursor(0, 55);
  M5.Lcd.print("Name: ");
  M5.Lcd.print(name.length() > 0 ? name : "Unknown");
  
  M5.Lcd.setCursor(0, 70);
  M5.Lcd.print("MAC: ");
  M5.Lcd.print(mac);
  
  M5.Lcd.setCursor(0, 85);
  M5.Lcd.print("Score: ");
  M5.Lcd.print(persistence, 2);
  
  M5.Lcd.setCursor(0, 105);
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.print("Press any button");
  
  while (!M5.BtnA.wasPressed() && !M5.BtnB.wasPressed()) {
    M5.update();
    delay(50);
  }
  
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
}

void displayTrackedDevices() {
  M5.Lcd.fillScreen(BLACK);
  
  M5.Lcd.drawFastHLine(0, 0, SCREEN_WIDTH, CYAN);
  M5.Lcd.drawFastHLine(0, 1, SCREEN_WIDTH, CYAN);
  
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 3);
  M5.Lcd.setTextColor(paused ? RED : GREEN);
  M5.Lcd.print(paused ? "PAUSE" : (scanningWiFi ? "WiFi" : "BLE"));
  
  M5.Lcd.setCursor(60, 3);
  M5.Lcd.setTextColor(BLUE_GREY);
  if (scanningWiFi) {
    M5.Lcd.print("APs:");
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.print(wifiDeviceIndex);
  } else {
    M5.Lcd.print("DEV:");
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.print(deviceIndex);
  }

  if (filterByName) {
    M5.Lcd.setCursor(190, 3);
    M5.Lcd.setTextColor(BLUE);
    M5.Lcd.print("[F]");
  }
  
  M5.Lcd.drawFastHLine(0, 11, SCREEN_WIDTH, CYAN);
  M5.Lcd.drawFastHLine(0, 12, SCREEN_WIDTH, CYAN);

  int y = 15;
  int displayed = 0;
  const int maxDisplay = 3;
  
  std::vector<DeviceInfo> allDevices;  // DECLARE HERE OUTSIDE IF BLOCK

  if (scanningWiFi) {
    // WiFi display code stays same
    for (int i = scrollIndex; i < wifiDeviceIndex && displayed < maxDisplay; i++, displayed++) {
      if (displayed > 0) {
        M5.Lcd.drawFastHLine(0, y - 2, SCREEN_WIDTH, BLUE_GREY);
        y += 1;
      }
      
      M5.Lcd.setTextColor(CYAN);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(2, y);
      String ssid = wifiDevices[i].ssid.length() > 0 ? wifiDevices[i].ssid : "Hidden";
      if (ssid.length() > 19) {
        ssid = ssid.substring(0, 16) + "...";
      }
      M5.Lcd.print(ssid);
      y += 17;
      
      M5.Lcd.setTextSize(1);
      M5.Lcd.setCursor(2, y);
      M5.Lcd.setTextColor(YELLOW);
      M5.Lcd.print("Ch:");
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.print(wifiDevices[i].channel);
      M5.Lcd.print(" ");
      M5.Lcd.setTextColor(GREEN);
      switch(wifiDevices[i].encryptionType) {
        case WIFI_AUTH_OPEN: M5.Lcd.print("OPEN"); break;
        case WIFI_AUTH_WEP: M5.Lcd.print("WEP"); break;
        case WIFI_AUTH_WPA_PSK: M5.Lcd.print("WPA"); break;
        case WIFI_AUTH_WPA2_PSK: M5.Lcd.print("WPA2"); break;
        default: M5.Lcd.print("WPA2"); break;
      }
      y += 9;
      
      M5.Lcd.setTextSize(1);
      M5.Lcd.setCursor(2, y);
      M5.Lcd.setTextColor(DARKGREY);
      M5.Lcd.print(wifiDevices[i].detectionCount);
      M5.Lcd.print("x ");
      M5.Lcd.print(wifiDevices[i].rssi);
      M5.Lcd.print("dB");
      
      M5.Lcd.setCursor(80, y);
      M5.Lcd.setTextColor(BLUE_GREY);
      M5.Lcd.print(wifiDevices[i].bssid.substring(9));
      y += 11;
    }
  } else {
    // BLE display
    std::vector<DeviceInfo> alertDevices;
    std::vector<DeviceInfo> normalDevices;

    for (int i = 0; i < deviceIndex; i++) {
      if (filterByName && trackedDevices[i].name.length() == 0) continue;
      
      if (trackedDevices[i].detected) {
        alertDevices.push_back(trackedDevices[i]);
      } else {
        normalDevices.push_back(trackedDevices[i]);
      }
    }

    std::sort(alertDevices.begin(), alertDevices.end(), 
      [](const DeviceInfo &a, const DeviceInfo &b) {
        return a.persistenceScore > b.persistenceScore;
      });

    std::sort(normalDevices.begin(), normalDevices.end(), 
      [](const DeviceInfo &a, const DeviceInfo &b) {
        return a.totalCount > b.totalCount;
      });

    allDevices = alertDevices;
    allDevices.insert(allDevices.end(), normalDevices.begin(), normalDevices.end());

    for (size_t i = scrollIndex; i < allDevices.size() && displayed < maxDisplay; i++, displayed++) {
      const auto &device = allDevices[i];
      
      if (displayed > 0) {
        M5.Lcd.drawFastHLine(0, y - 2, SCREEN_WIDTH, BLUE_GREY);
        y += 1;
      }
      
      if (device.isSpecial) {
        M5.Lcd.setTextColor(BLUE);
      } else if (device.detected) {
        M5.Lcd.setTextColor(RED);
      } else {
        M5.Lcd.setTextColor(CYAN);
      }
      
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(2, y);
      
      String displayName = (device.name.length() > 0) ? device.name : "Unknown";
      if (displayName.length() > 19) {
        displayName = displayName.substring(0, 16) + "...";
      }
      M5.Lcd.print(displayName);
      y += 17;
      
      M5.Lcd.setTextSize(1);
      M5.Lcd.setCursor(2, y);
      M5.Lcd.setTextColor(YELLOW);
      
      String mfg = device.manufacturer;
      if (mfg.length() > 30) {
        mfg = mfg.substring(0, 27) + "...";
      }
      M5.Lcd.print(mfg);
      y += 9;
      
      M5.Lcd.setTextSize(1);
      M5.Lcd.setCursor(2, y);
      
      if (device.detected) {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.print("!");
        M5.Lcd.setTextColor(YELLOW);
        M5.Lcd.print(device.persistenceScore, 2);
      } else {
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.print(device.totalCount);
        M5.Lcd.print("x ");
        M5.Lcd.print(device.lastRssi);
        M5.Lcd.print("dB");
      }
      
      M5.Lcd.setCursor(80, y);
      M5.Lcd.setTextColor(BLUE_GREY);
      M5.Lcd.print(device.address);
      y += 11;
    }
  }
  
  M5.Lcd.drawFastHLine(0, 133, SCREEN_WIDTH, CYAN);
  M5.Lcd.drawFastHLine(0, 134, SCREEN_WIDTH, CYAN);
  
  int total = scanningWiFi ? wifiDeviceIndex : (int)allDevices.size();
  if (total > 0) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.setCursor(2, 124);
    
    int showing = min(maxDisplay, total - scrollIndex);
    M5.Lcd.print(scrollIndex + 1);
    M5.Lcd.print("-");
    M5.Lcd.print(scrollIndex + showing);
    M5.Lcd.print("/");
    M5.Lcd.print(total);
    
    if (paused && total > maxDisplay) {
      M5.Lcd.setCursor(80, 124);
      M5.Lcd.setTextColor(GREEN);
      M5.Lcd.print("A/B:Scroll");
    }
  }
}

void displayMenuScreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(2, 2);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setTextSize(1);
  M5.Lcd.print("SETTINGS");
  
  M5.Lcd.drawLine(0, 12, SCREEN_WIDTH, 12, DARKGREY);

  int y = 16;

  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(2, y);
  M5.Lcd.print("Battery: ");
  float batVoltage = M5.Axp.GetBatVoltage();
  M5.Lcd.print(batVoltage, 2);
  M5.Lcd.print("V");
  
  int batPercent = (int)((batVoltage - 3.0) / 1.2 * 100.0);
  if (batPercent > 100) batPercent = 100;
  if (batPercent < 0) batPercent = 0;
  M5.Lcd.print(" (");
  M5.Lcd.print(batPercent);
  M5.Lcd.print("%)");
  y += 12;

  M5.Lcd.setCursor(2, y);
  M5.Lcd.print("Brightness: ");
  M5.Lcd.print(highBrightness ? "High" : "Low");
  y += 12;

  M5.Lcd.setCursor(2, y);
  M5.Lcd.print("Tracked: ");
  M5.Lcd.print(deviceIndex);
  M5.Lcd.print("  Ignored: ");
  M5.Lcd.print(ignoreList.size());
  y += 16;

  M5.Lcd.drawLine(0, y, SCREEN_WIDTH, y, DARKGREY);
  y += 4;

  // Menu options - compact
  int baseY = y;
  int optionY[3];
  
  M5.Lcd.setTextColor(CYAN);
  
  optionY[0] = y;
  M5.Lcd.setCursor(10, y);
  M5.Lcd.print("Toggle Brightness");
  y += 11;
  
  optionY[1] = y;
  M5.Lcd.setCursor(10, y);
  M5.Lcd.print("Clear Devices");
  y += 11;
  
  optionY[2] = y;
  M5.Lcd.setCursor(10, y);
  M5.Lcd.print("Shutdown");
  y += 14;

  // Instructions at bottom
  M5.Lcd.drawLine(0, y, SCREEN_WIDTH, y, DARKGREY);
  y += 3;
  
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.setCursor(2, y);
  M5.Lcd.print("A:Nav B:Select A+B:Exit");
  
  // Store baseY globally for highlighting
  menuBaseY = baseY;
}

void highlightMenuOption(int index) {
  // Clear all highlights
  for (int i = 0; i < 3; i++) {
    M5.Lcd.setCursor(2, menuBaseY + (i * 11));
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.print(">");
  }
  
  // Draw current highlight
  M5.Lcd.setCursor(2, menuBaseY + (index * 11));
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.print(">");
}

void executeMenuOption(int index) {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.setCursor(30, 50);
  
  switch (index) {
    case 0:
      toggleBrightness();
      M5.Lcd.print("Brightness");
      M5.Lcd.setCursor(40, 70);
      M5.Lcd.print(highBrightness ? "HIGH" : "LOW");
      break;
    case 1:
      clearDevices();
      M5.Lcd.print("Cleared!");
      break;
    case 2:
      shutdownDevice();
      return; // Don't refresh menu
  }
  
  M5.Lcd.setTextSize(1);
  delay(1000);
  displayMenuScreen();
  highlightMenuOption(menuIndex);
}

void toggleBrightness() {
  highBrightness = !highBrightness;
  M5.Axp.ScreenBreath(highBrightness ? 80 : 30);
  lastButtonPressTime = millis(); // Reset dim timer
}

void clearDevices() {
  deviceIndex = 0;
  scrollIndex = 0;
  SPIFFS.remove("/devices.txt");
}

void shutdownDevice() {
  saveDeviceData();
  M5.Lcd.fillScreen(RED);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(20, 50);
  M5.Lcd.print("Shutting Down");
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(40, 80);
  M5.Lcd.print("Goodbye!");
  delay(2000);
  M5.Axp.PowerOff();
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

void saveDeviceData() {
  File file = SPIFFS.open("/devices.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  for (int i = 0; i < deviceIndex; i++) {
    file.print(trackedDevices[i].address);
    file.print(",");
    file.print(trackedDevices[i].lastSeen);
    file.print(",");
    file.print(trackedDevices[i].totalCount);
    file.print(",");
    file.println(trackedDevices[i].persistenceScore);
  }

  file.close();
  Serial.println("Device data saved");
}

void loadDeviceData() {
  File file = SPIFFS.open("/devices.txt", FILE_READ);
  if (!file) {
    Serial.println("No saved devices");
    return;
  }

  deviceIndex = 0;
  while (file.available() && deviceIndex < MAX_DEVICES) {
    String line = file.readStringUntil('\n');
    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);
    int comma3 = line.indexOf(',', comma2 + 1);
    
    if (comma1 > 0 && comma2 > 0) {
      DeviceInfo device;
      device.address = line.substring(0, comma1);
      device.lastSeen = line.substring(comma1 + 1, comma2).toInt();
      device.totalCount = (comma3 > 0) ? line.substring(comma2 + 1, comma3).toInt() : 0;
      device.persistenceScore = (comma3 > 0) ? line.substring(comma3 + 1).toFloat() : 0.0;
      device.name = "";
      device.manufacturer = getManufacturer(device.address.c_str());
      device.firstSeen = device.lastSeen;
      device.rssiSum = 0;
      device.rssiCount = 0;
      device.lastRssi = 0;
      device.detected = false;
      device.isSpecial = isSpecialMac(device.address.c_str());
      device.alertTriggered = false;
      device.stableRssiCount = 0;
      device.variationCount = 0;
      
      for (int w = 0; w < 4; w++) {
        device.windows[w].start = 0;
        device.windows[w].end = 0;
        device.windows[w].detections = 0;
      }
      
      trackedDevices[deviceIndex++] = device;
    }
  }

  file.close();
  Serial.print("Loaded ");
  Serial.print(deviceIndex);
  Serial.println(" devices");
}

void loadIgnoreList() {
  File file = SPIFFS.open("/ignore_list.txt", FILE_READ);
  if (!file) {
    Serial.println("No ignore list found");
    return;
  }

  while (file.available()) {
    String mac = file.readStringUntil('\n');
    mac.trim();
    if (mac.length() > 0) {
      ignoreList.insert(mac);
    }
  }
  file.close();
  Serial.print("Loaded ");
  Serial.print(ignoreList.size());
  Serial.println(" ignored MACs");
}