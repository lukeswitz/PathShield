#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "MacPrefixes.h"
#include <algorithm>
#include <set>
#include <SPIFFS.h>
#include <WiFi.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// Memory constants, tweak for traffic density/memory
#define MAX_DEVICES 50
#define MAX_WIFI_DEVICES 50
#define DETECTION_WINDOW 300
#define IDLE_TIMEOUT 30000
#define MAX_TIMESTAMPS 16

#define BLUE_GREY 0x5D9B

#define WIFI_NAME_COLOR GREEN
#define BLE_NAME_COLOR  CYAN

#define WINDOW_RECENT 300
#define WINDOW_MEDIUM 600
#define WINDOW_OLD 900
#define WINDOW_OLDEST 1200

#define MIN_DETECTIONS 8
#define MIN_WINDOWS 3
#define PERSISTENCE_THRESHOLD 0.65
#define RSSI_STABILITY_THRESHOLD 10
#define RSSI_VARIATION_THRESHOLD 20
#define EPSILON_CONNECTED_GAP 180

#define ESTIMATED_BLE_STACK_KB 75
#define ESTIMATED_WIFI_STACK_KB 60
#define ESTIMATED_APP_USAGE_KB 30
#define AVAILABLE_FOR_DEVICES_KB 100

const int MEMORY_CRITICAL = 50;
const int MEMORY_WARNING = 80;
const int MEMORY_GOOD = 150;

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
  char ssid[33];
  char bssid[18];
  int rssi;
  int channel;
  int encryptionType;
  unsigned long lastSeen;
  int detectionCount;
};

WiFiDeviceInfo wifiDevices[MAX_WIFI_DEVICES];
int wifiDeviceIndex = 0;
bool scanningWiFi = true;
unsigned long lastScanSwitch = 0;
const unsigned long SCAN_SWITCH_INTERVAL = 3000;

// Privacy Invader Defaults: Axon cameras, Liteon Technology (Flock), Utility Inc (Flock) OUIs
const char *specialMacs[] = {"00:25:DF", "14:5A:FC", "00:09:BC"};
// Example: "AA:BB:CC" - add trusted device OUIs here
const char *allowlistMacs[] = {""};

struct TimeWindow {
  unsigned long start;
  unsigned long end;
  int detections;
};

struct DeviceInfo {
  char address[18];
  char name[21];
  char manufacturer[31];
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
  TimeWindow windows[4];
  unsigned long timestamps[MAX_TIMESTAMPS];
  uint8_t tsIdx;
  uint8_t tsCount;
};

DeviceInfo trackedDevices[MAX_DEVICES];
int deviceIndex = 0;
BLEScan *pBLEScan;
unsigned long lastButtonPressTime = 0;
int scrollIndex = 0;
std::set<String> ignoreList;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {}
};

bool isSpecialMac(const char *address) {
  for (size_t i = 0; i < sizeof(specialMacs) / sizeof(specialMacs[0]); i++) {
    if (strncmp(address, specialMacs[i], strlen(specialMacs[i])) == 0) {
      return true;
    }
  }
  return false;
}

bool isAllowlistedMac(const char *address) {
  for (size_t i = 0; i < sizeof(allowlistMacs) / sizeof(allowlistMacs[0]); i++) {
    if (strlen(allowlistMacs[i]) > 0 &&
        strncmp(address, allowlistMacs[i], strlen(allowlistMacs[i])) == 0) {
      return true;
    }
  }
  return false;
}

void trackWiFiDevice(const char *ssid, const char *bssid, int rssi, int channel,
                     int encType, unsigned long currentTime) {
  for (int i = 0; i < wifiDeviceIndex; i++) {
    if (strcmp(wifiDevices[i].bssid, bssid) == 0) {
      wifiDevices[i].rssi = rssi;
      wifiDevices[i].lastSeen = currentTime;
      wifiDevices[i].detectionCount++;
      return;
    }
  }

  if (wifiDeviceIndex < MAX_WIFI_DEVICES) {
    strncpy(wifiDevices[wifiDeviceIndex].ssid, ssid, 32);
    wifiDevices[wifiDeviceIndex].ssid[32] = '\0';
    strncpy(wifiDevices[wifiDeviceIndex].bssid, bssid, 17);
    wifiDevices[wifiDeviceIndex].bssid[17] = '\0';
    wifiDevices[wifiDeviceIndex].rssi = rssi;
    wifiDevices[wifiDeviceIndex].channel = channel;
    wifiDevices[wifiDeviceIndex].encryptionType = encType;
    wifiDevices[wifiDeviceIndex].lastSeen = currentTime;
    wifiDevices[wifiDeviceIndex].detectionCount = 1;
    wifiDeviceIndex++;
  } else {
    int oldestIndex = -1;
    unsigned long oldestTime = currentTime;

    for (int i = 0; i < wifiDeviceIndex; i++) {
      if (wifiDevices[i].lastSeen < oldestTime) {
        oldestTime = wifiDevices[i].lastSeen;
        oldestIndex = i;
      }
    }

    if (oldestIndex != -1) {
      strncpy(wifiDevices[oldestIndex].ssid, ssid, 32);
      wifiDevices[oldestIndex].ssid[32] = '\0';
      strncpy(wifiDevices[oldestIndex].bssid, bssid, 17);
      wifiDevices[oldestIndex].bssid[17] = '\0';
      wifiDevices[oldestIndex].rssi = rssi;
      wifiDevices[oldestIndex].channel = channel;
      wifiDevices[oldestIndex].encryptionType = encType;
      wifiDevices[oldestIndex].lastSeen = currentTime;
      wifiDevices[oldestIndex].detectionCount = 1;
    }
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

void updateTimeWindows(DeviceInfo &device, unsigned long currentTime) {
  device.windows[0].start =
      currentTime >= WINDOW_RECENT ? currentTime - WINDOW_RECENT : 0;
  device.windows[0].end = currentTime;
  device.windows[1].start =
      currentTime >= WINDOW_MEDIUM ? currentTime - WINDOW_MEDIUM : 0;
  device.windows[1].end = device.windows[0].start;
  device.windows[2].start =
      currentTime >= WINDOW_OLD ? currentTime - WINDOW_OLD : 0;
  device.windows[2].end = device.windows[1].start;
  device.windows[3].start =
      currentTime >= WINDOW_OLDEST ? currentTime - WINDOW_OLDEST : 0;
  device.windows[3].end = device.windows[2].start;

  for (int w = 0; w < 4; w++) {
    device.windows[w].detections = 0;
    for (int i = 0; i < device.tsCount; i++) {
      if (device.timestamps[i] >= device.windows[w].start &&
          device.timestamps[i] < device.windows[w].end) {
        device.windows[w].detections++;
      }
    }
  }
}

float calculatePersistenceScore(DeviceInfo &device, unsigned long currentTime) {
  float score = 0.0f;
  int factors = 0;

  if (device.totalCount >= MIN_DETECTIONS) {
    score += 0.25f * (float)std::min(device.totalCount, 30) / 30.0f;
    factors++;
  }

  int windowsActive = 0;
  for (int i = 0; i < 4; i++) {
    if (device.windows[i].detections > 0) windowsActive++;
  }
  if (windowsActive >= MIN_WINDOWS) {
    score += 0.30f * (float)windowsActive / 4.0f;
    factors++;
  }

  bool isConnected = true;
  if (device.tsCount >= 3) {
    for (int i = 1; i < device.tsCount; i++) {
      int prev =
          (device.tsIdx - device.tsCount + i - 1 + MAX_TIMESTAMPS) % MAX_TIMESTAMPS;
      int curr =
          (device.tsIdx - device.tsCount + i + MAX_TIMESTAMPS) % MAX_TIMESTAMPS;
      if (device.timestamps[curr] - device.timestamps[prev] >
          EPSILON_CONNECTED_GAP) {
        isConnected = false;
        break;
      }
    }
    if (isConnected) {
      score += 0.25f;
      factors++;
    }
  }

  if (device.stableRssiCount > 5 || device.variationCount > 5) {
    score += 0.20f * (float)std::min(device.variationCount, 10) / 10.0f;
    factors++;
  }

  return (factors > 0) ? score : 0.0f;
}

void moveToTop(int index) {
  if (index <= 0) return;
  DeviceInfo temp = trackedDevices[index];
  for (int i = index; i > 0; i--) {
    trackedDevices[i] = trackedDevices[i - 1];
  }
  trackedDevices[0] = temp;
}

int findEvictionCandidate() {
  int bestCandidate = -1;
  float minScore = 100.0f;
  unsigned long oldestSeen = 0xFFFFFFFF;

  for (int i = 0; i < deviceIndex; i++) {
    if (trackedDevices[i].isSpecial || trackedDevices[i].alertTriggered) continue;

    if (trackedDevices[i].persistenceScore < minScore) {
      minScore = trackedDevices[i].persistenceScore;
      bestCandidate = i;
      oldestSeen = trackedDevices[i].lastSeen;
    } else if (trackedDevices[i].persistenceScore == minScore) {
      if (trackedDevices[i].lastSeen < oldestSeen) {
        bestCandidate = i;
        oldestSeen = trackedDevices[i].lastSeen;
      }
    }
  }

  if (bestCandidate == -1 && deviceIndex > 0) {
    oldestSeen = 0xFFFFFFFF;
    for (int i = 0; i < deviceIndex; i++) {
      if (trackedDevices[i].lastSeen < oldestSeen) {
        bestCandidate = i;
        oldestSeen = trackedDevices[i].lastSeen;
      }
    }
  }

  return bestCandidate;
}

bool trackDevice(const char *address, int rssi, unsigned long currentTime,
                 const char *name) {
  bool newTracker = false;

  for (int i = 0; i < deviceIndex; i++) {
    if (strcmp(trackedDevices[i].address, address) == 0) {
      trackedDevices[i].totalCount++;
      trackedDevices[i].lastSeen = currentTime;
      trackedDevices[i].rssiSum += rssi;
      trackedDevices[i].rssiCount++;

      trackedDevices[i].timestamps[trackedDevices[i].tsIdx] = currentTime;
      trackedDevices[i].tsIdx =
          (trackedDevices[i].tsIdx + 1) % MAX_TIMESTAMPS;
      if (trackedDevices[i].tsCount < MAX_TIMESTAMPS)
        trackedDevices[i].tsCount++;

      int rssiDiff = abs(trackedDevices[i].lastRssi - rssi);
      if (rssiDiff <= RSSI_STABILITY_THRESHOLD) {
        trackedDevices[i].stableRssiCount++;
      } else {
        trackedDevices[i].stableRssiCount = 0;
      }
      if (rssiDiff >= RSSI_VARIATION_THRESHOLD) {
        trackedDevices[i].variationCount++;
      }
      trackedDevices[i].lastRssi = rssi;

      if (name && strlen(name) > 0) {
        strncpy(trackedDevices[i].name, name, 20);
        trackedDevices[i].name[20] = '\0';
      }

      String mfg = getManufacturer(address);
      strncpy(trackedDevices[i].manufacturer, mfg.c_str(), 30);
      trackedDevices[i].manufacturer[30] = '\0';

      updateTimeWindows(trackedDevices[i], currentTime);
      trackedDevices[i].persistenceScore =
          calculatePersistenceScore(trackedDevices[i], currentTime);

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
      return newTracker;
    }
  }

  int targetIndex = -1;

  if (deviceIndex < MAX_DEVICES) {
    for (int j = deviceIndex; j > 0; j--) {
      trackedDevices[j] = trackedDevices[j - 1];
    }
    targetIndex = 0;
    deviceIndex++;
  } else {
    int evictIdx = findEvictionCandidate();
    if (evictIdx != -1) {
      for (int j = evictIdx; j > 0; j--) {
        trackedDevices[j] = trackedDevices[j - 1];
      }
      targetIndex = 0;
    }
  }

  if (targetIndex == 0) {
    memset(&trackedDevices[0], 0, sizeof(DeviceInfo));
    strncpy(trackedDevices[0].address, address, 17);
    trackedDevices[0].address[17] = '\0';

    if (name && strlen(name) > 0) {
      strncpy(trackedDevices[0].name, name, 20);
      trackedDevices[0].name[20] = '\0';
    }

    String mfg = getManufacturer(address);
    strncpy(trackedDevices[0].manufacturer, mfg.c_str(), 30);
    trackedDevices[0].manufacturer[30] = '\0';

    trackedDevices[0].totalCount = 1;
    trackedDevices[0].firstSeen = currentTime;
    trackedDevices[0].lastSeen = currentTime;
    trackedDevices[0].rssiSum = rssi;
    trackedDevices[0].rssiCount = 1;
    trackedDevices[0].lastRssi = rssi;
    trackedDevices[0].timestamps[0] = currentTime;
    trackedDevices[0].tsIdx = 1;
    trackedDevices[0].tsCount = 1;

    if (isSpecialMac(address)) {
      trackedDevices[0].detected = true;
      trackedDevices[0].isSpecial = true;
      trackedDevices[0].alertTriggered = true;
      newTracker = true;
    }
  }

  return newTracker;
}

void removeOldEntries(unsigned long currentTime) {
  for (int i = 0; i < deviceIndex; i++) {
    if (trackedDevices[i].alertTriggered) continue;

    if (currentTime - trackedDevices[i].lastSeen > DETECTION_WINDOW) {
      for (int j = i; j < deviceIndex - 1; j++) {
        trackedDevices[j] = trackedDevices[j + 1];
      }
      deviceIndex--;
      i--;
    }
  }
}

void alertUser(bool isSpecial, const char *name, const char *mac, float persistence) {
  if (isSpecial) {
    for (int i = 0; i < 5; i++) {
      M5.Display.fillScreen(RED);
      delay(200);
      M5.Display.fillScreen(BLUE);
      delay(200);
    }
  } else {
    M5.Display.fillScreen(RED);
  }

  M5.Display.setCursor(0, 10);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(2);
  M5.Display.print("Tracker Detected!");
  M5.Display.setTextSize(1);

  M5.Display.setCursor(0, 40);
  M5.Display.print("Type: ");
  M5.Display.print(isSpecial ? "KNOWN" : "SUSPECTED");

  M5.Display.setCursor(0, 55);
  M5.Display.print("Name: ");
  M5.Display.print(strlen(name) > 0 ? name : "Unknown");

  M5.Display.setCursor(0, 70);
  M5.Display.print("MAC: ");
  M5.Display.print(mac);

  M5.Display.setCursor(0, 85);
  M5.Display.print("Score: ");
  M5.Display.print(persistence, 2);

  M5.Display.setCursor(0, 105);
  M5.Display.setTextColor(YELLOW);
  M5.Display.print("Press any button");

  while (!M5.BtnA.wasPressed() && !M5.BtnB.wasPressed()) {
    M5.update();
    delay(50);
  }

  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(1);
}

void displayStartupMessage() {
  M5.Display.fillScreen(BLACK);

  for (int i = 0; i < 20; i++) {
    int x = random(0, SCREEN_WIDTH);
    int y = random(0, SCREEN_HEIGHT);
    int w = random(5, 40);
    M5.Display.drawFastHLine(x, y, w, random(0x0000, 0x1111));
  }

  M5.Display.drawFastHLine(0, 20, SCREEN_WIDTH, CYAN);
  M5.Display.drawFastHLine(0, 21, SCREEN_WIDTH, CYAN);

  M5.Display.setTextSize(3);
  M5.Display.setTextColor(CYAN);
  M5.Display.setCursor(15, 30);
  M5.Display.print("PATH");
  M5.Display.setTextColor(MAGENTA);
  M5.Display.print("SHIELD");

  M5.Display.setTextColor(0x07E0);
  M5.Display.setCursor(16, 31);
  M5.Display.print("PATH");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(YELLOW);
  M5.Display.setCursor(50, 60);
  M5.Display.print("TRACKER DETECTION");

  M5.Display.setTextColor(DARKGREY);
  M5.Display.setCursor(85, 72);
  M5.Display.print("v2.4 Color");

  M5.Display.drawFastHLine(0, 85, SCREEN_WIDTH, MAGENTA);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(GREEN);

  M5.Display.setCursor(10, 95);
  M5.Display.print("> Initializing BLE/WiFi");
  delay(400);

  M5.Display.setCursor(190, 95);
  M5.Display.setTextColor(CYAN);
  M5.Display.print("[OK]");
  delay(300);

  M5.Display.setTextColor(GREEN);
  M5.Display.setCursor(10, 107);
  M5.Display.print("> Loading SPIFFS");
  delay(400);

  M5.Display.setCursor(190, 107);
  M5.Display.setTextColor(CYAN);
  M5.Display.print("[OK]");
  delay(300);

  M5.Display.setTextColor(GREEN);
  M5.Display.setCursor(10, 119);
  M5.Display.print("> System Ready");
  delay(300);

  M5.Display.setCursor(190, 119);
  M5.Display.setTextColor(CYAN);
  M5.Display.print("[OK]");

  delay(400);
  M5.Display.fillScreen(CYAN);
  delay(50);
  M5.Display.fillScreen(BLACK);
  delay(50);
}

void drawTopBar() {
  M5.Display.drawFastHLine(0, 0, SCREEN_WIDTH, CYAN);
  M5.Display.drawFastHLine(0, 1, SCREEN_WIDTH, CYAN);

  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 3);
  M5.Display.setTextColor(paused ? RED : GREEN);
  M5.Display.print(paused ? "PAUSE" : (scanningWiFi ? "WiFi" : "BLE"));

  float batVoltage = M5.Power.getBatteryVoltage() / 1000.0f;
  int batPercent = (int)((batVoltage - 3.0f) / 1.2f * 100.0f);
  if (batPercent > 100) batPercent = 100;
  if (batPercent < 0) batPercent = 0;

  int barWidth = 30;
  int barX = 75;
  int barY = 4;

  uint16_t barColor = GREEN;
  if (batPercent < 50) barColor = YELLOW;
  if (batPercent < 25) barColor = RED;

  M5.Display.setTextColor(BLUE_GREY);
  M5.Display.setCursor(45, 3);
  M5.Display.print("B:");

  M5.Display.drawRect(barX, barY, barWidth, 6, BLUE_GREY);
  int filledWidth = (barWidth - 2) * batPercent / 100;
  M5.Display.fillRect(barX + 1, barY + 1, filledWidth, 4, barColor);

  M5.Display.setTextColor(DARKGREY);
  M5.Display.setCursor(110, 3);
  M5.Display.print(batPercent);
  M5.Display.print("%");

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t freeKB = freeHeap / 1024;

  // Scale memory display: 20 KB = 100%, 5 KB = 0%
  const int MEMORY_MAX_KB = 20;
  const int MEMORY_MIN_KB = 5;
  
  int memPercent = ((freeKB - MEMORY_MIN_KB) * 100) / (MEMORY_MAX_KB - MEMORY_MIN_KB);
  if (memPercent > 100) memPercent = 100;
  if (memPercent < 0) memPercent = 0;

  barX = 155;
  uint16_t memColor = GREEN;
  if (memPercent < 40) memColor = YELLOW;
  if (memPercent < 20) memColor = RED;

  M5.Display.setCursor(135, 3);
  M5.Display.setTextColor(BLUE_GREY);
  M5.Display.print("M:");

  M5.Display.drawRect(barX, barY, barWidth, 6, BLUE_GREY);
  filledWidth = (barWidth - 2) * memPercent / 100;
  M5.Display.fillRect(barX + 1, barY + 1, filledWidth, 4, memColor);

  M5.Display.setTextColor(memColor);
  M5.Display.setCursor(190, 3);
  M5.Display.print(freeKB);
  M5.Display.print("KB");

  M5.Display.drawFastHLine(0, 11, SCREEN_WIDTH, CYAN);
  M5.Display.drawFastHLine(0, 12, SCREEN_WIDTH, CYAN);
}

void displayTrackedDevices() {
  M5.Display.fillScreen(BLACK);

  drawTopBar();

  int y = 15;
  int displayed = 0;
  const int maxDisplay = 3;
  int totalItems = 0;

  if (scanningWiFi) {
    totalItems = wifiDeviceIndex;
    for (int i = scrollIndex; i < wifiDeviceIndex && displayed < maxDisplay;
         i++, displayed++) {
      if (displayed > 0) {
        M5.Display.drawFastHLine(0, y - 2, SCREEN_WIDTH, BLUE_GREY);
        y += 1;
      }

      M5.Display.setTextColor(WIFI_NAME_COLOR); // CYAN for WiFi
      M5.Display.setTextSize(2);
      M5.Display.setCursor(2, y);
      char ssidDisplay[20];
      if (strlen(wifiDevices[i].ssid) > 0) {
        strncpy(ssidDisplay, wifiDevices[i].ssid, 16);
        ssidDisplay[16] = '\0';
        if (strlen(wifiDevices[i].ssid) > 16) strcat(ssidDisplay, "...");
      } else {
        strcpy(ssidDisplay, "Hidden");
      }
      M5.Display.print(ssidDisplay);
      y += 17;

      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y);
      M5.Display.setTextColor(YELLOW);
      String mfg = getManufacturer(wifiDevices[i].bssid);
      char mfgDisplay[28];
      strncpy(mfgDisplay, mfg.c_str(), 27);
      mfgDisplay[27] = '\0';
      M5.Display.print(mfgDisplay);
      y += 9;

      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y);
      M5.Display.setTextColor(WHITE);
      M5.Display.print("Ch");
      M5.Display.print(wifiDevices[i].channel);
      M5.Display.print(" ");
      M5.Display.setTextColor(GREEN);
      switch (wifiDevices[i].encryptionType) {
        case WIFI_AUTH_OPEN: M5.Display.print("OPEN"); break;
        case WIFI_AUTH_WEP: M5.Display.print("WEP"); break;
        case WIFI_AUTH_WPA_PSK: M5.Display.print("WPA"); break;
        case WIFI_AUTH_WPA2_PSK: M5.Display.print("WPA2"); break;
        case WIFI_AUTH_WPA_WPA2_PSK: M5.Display.print("WPA/2"); break;
        case WIFI_AUTH_WPA2_ENTERPRISE: M5.Display.print("WPA2-E"); break;
        case WIFI_AUTH_WPA3_PSK: M5.Display.print("WPA3"); break;
        case WIFI_AUTH_WPA2_WPA3_PSK: M5.Display.print("WPA2/3"); break;
        case WIFI_AUTH_WAPI_PSK: M5.Display.print("WAPI"); break;
        case WIFI_AUTH_OWE: M5.Display.print("OWE"); break;
        case WIFI_AUTH_WPA3_ENT_192: M5.Display.print("WPA3-E"); break;
        default: M5.Display.print("UNK"); break;
      }
      M5.Display.print(" ");
      M5.Display.setTextColor(DARKGREY);
      M5.Display.print(wifiDevices[i].detectionCount);
      M5.Display.print("x ");
      M5.Display.print(wifiDevices[i].rssi);
      M5.Display.print("dB");
      y += 11;
    }
  } else {
    int filteredCount = 0;
    int sortedIndices[MAX_DEVICES];

    for (int i = 0; i < deviceIndex; i++) {
      if (filterByName && strlen(trackedDevices[i].name) == 0) continue;
      sortedIndices[filteredCount++] = i;
    }

    for (int i = 0; i < filteredCount - 1; i++) {
      for (int j = i + 1; j < filteredCount; j++) {
        bool swap = false;
        if (trackedDevices[sortedIndices[i]].detected &&
            trackedDevices[sortedIndices[j]].detected) {
          swap = trackedDevices[sortedIndices[i]].persistenceScore <
                 trackedDevices[sortedIndices[j]].persistenceScore;
        } else if (!trackedDevices[sortedIndices[i]].detected &&
                   trackedDevices[sortedIndices[j]].detected) {
          swap = true;
        } else if (!trackedDevices[sortedIndices[i]].detected &&
                   !trackedDevices[sortedIndices[j]].detected) {
          swap = trackedDevices[sortedIndices[i]].totalCount <
                 trackedDevices[sortedIndices[j]].totalCount;
        }
        if (swap) {
          int temp = sortedIndices[i];
          sortedIndices[i] = sortedIndices[j];
          sortedIndices[j] = temp;
        }
      }
    }

    totalItems = filteredCount;

    for (int idx = scrollIndex; idx < filteredCount && displayed < maxDisplay;
         idx++, displayed++) {
      int i = sortedIndices[idx];

      if (displayed > 0) {
        M5.Display.drawFastHLine(0, y - 2, SCREEN_WIDTH, BLUE_GREY);
        y += 1;
      }

      // Default color logic
      if (trackedDevices[i].isSpecial) {
        M5.Display.setTextColor(ORANGE);
      } else if (trackedDevices[i].detected) {
        M5.Display.setTextColor(RED);
      } else {
         // This is the main change: regular devices get the BLE color
         M5.Display.setTextColor(BLE_NAME_COLOR);
      }

      M5.Display.setTextSize(2);
      M5.Display.setCursor(2, y);

      char nameDisplay[20];
      if (strlen(trackedDevices[i].name) > 0) {
        strncpy(nameDisplay, trackedDevices[i].name, 16);
        nameDisplay[16] = '\0';
        if (strlen(trackedDevices[i].name) > 16) strcat(nameDisplay, "...");
      } else {
        strcpy(nameDisplay, "Unknown");
      }
      M5.Display.print(nameDisplay);
      y += 17;

      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y);
      M5.Display.setTextColor(YELLOW);

      char mfgDisplay[28];
      strncpy(mfgDisplay, trackedDevices[i].manufacturer, 27);
      mfgDisplay[27] = '\0';
      M5.Display.print(mfgDisplay);
      y += 9;

      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y);

      if (trackedDevices[i].detected) {
        M5.Display.setTextColor(RED);
        M5.Display.print("!");
        M5.Display.setTextColor(YELLOW);
        M5.Display.print(trackedDevices[i].persistenceScore, 2);
      } else {
        M5.Display.setTextColor(WHITE);
        M5.Display.print(trackedDevices[i].totalCount);
        M5.Display.print("x ");
        M5.Display.print(trackedDevices[i].lastRssi);
        M5.Display.print("dB");
      }

      M5.Display.setCursor(80, y);
      M5.Display.setTextColor(BLUE_GREY);
      M5.Display.print(trackedDevices[i].address);
      y += 11;
    }
  }

  M5.Display.drawFastHLine(0, 133, SCREEN_WIDTH, CYAN);
  M5.Display.drawFastHLine(0, 134, SCREEN_WIDTH, CYAN);

  if (totalItems > 0) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(YELLOW);
    M5.Display.setCursor(2, 124);

    int showing = std::min(maxDisplay, totalItems - scrollIndex);
    M5.Display.print(scrollIndex + 1);
    M5.Display.print("-");
    M5.Display.print(scrollIndex + showing);
    M5.Display.print("/");
    M5.Display.print(totalItems);

    if (paused && totalItems > maxDisplay) {
      M5.Display.setCursor(80, 124);
      M5.Display.setTextColor(GREEN);
      M5.Display.print("A/B:Scroll");
    }
  }
}

void displayMenuScreen() {
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(2, 2);
  M5.Display.setTextColor(GREEN);
  M5.Display.setTextSize(1);
  M5.Display.print("SETTINGS");

  M5.Display.drawLine(0, 12, SCREEN_WIDTH, 12, DARKGREY);

  int y = 16;

  M5.Display.setTextColor(WHITE);
  M5.Display.setCursor(2, y);
  M5.Display.print("Battery: ");
  float batVoltage = M5.Power.getBatteryVoltage() / 1000.0f;
  M5.Display.print(batVoltage, 2);
  M5.Display.print("V");

  int batPercent = (int)((batVoltage - 3.0f) / 1.2f * 100.0f);
  if (batPercent > 100) batPercent = 100;
  if (batPercent < 0) batPercent = 0;
  M5.Display.print(" (");
  M5.Display.print(batPercent);
  M5.Display.print("%)");
  y += 12;

  M5.Display.setCursor(2, y);
  M5.Display.print("Brightness: ");
  M5.Display.print(highBrightness ? "High" : "Low");
  y += 12;

  M5.Display.setCursor(2, y);
  M5.Display.print("Tracked: ");
  M5.Display.print(deviceIndex);
  M5.Display.print("  Ignored: ");
  M5.Display.print(ignoreList.size());
  y += 12;

  M5.Display.setCursor(2, y);
  M5.Display.print("Free RAM: ");
  M5.Display.print(ESP.getFreeHeap() / 1024);
  M5.Display.print(" KB");
  y += 16;

  M5.Display.drawLine(0, y, SCREEN_WIDTH, y, DARKGREY);
  y += 4;

  menuBaseY = y;

  M5.Display.setTextColor(CYAN);

  M5.Display.setCursor(10, y);
  M5.Display.print("Toggle Brightness");
  y += 11;

  M5.Display.setCursor(10, y);
  M5.Display.print("Clear Devices");
  y += 11;

  M5.Display.setCursor(10, y);
  M5.Display.print("Shutdown");
  y += 14;

  M5.Display.drawLine(0, y, SCREEN_WIDTH, y, DARKGREY);
  y += 3;

  M5.Display.setTextColor(YELLOW);
  M5.Display.setCursor(2, y);
  M5.Display.print("A:Nav B:Select A+B:Exit");
}

void highlightMenuOption(int index) {
  for (int i = 0; i < 3; i++) {
    M5.Display.setCursor(2, menuBaseY + (i * 11));
    M5.Display.setTextColor(BLACK);
    M5.Display.print(">");
  }

  M5.Display.setCursor(2, menuBaseY + (index * 11));
  M5.Display.setTextColor(YELLOW);
  M5.Display.print(">");
}

void toggleBrightness() {
  highBrightness = !highBrightness;
  M5.Display.setBrightness(highBrightness ? 204 : 77);
  lastButtonPressTime = millis();
}

void saveDeviceData() {
  File file = SPIFFS.open("/devices.txt", FILE_WRITE);
  if (!file) return;

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
}

void clearDevices() {
  deviceIndex = 0;
  scrollIndex = 0;
  SPIFFS.remove("/devices.txt");
}

void shutdownDevice() {
  saveDeviceData();
  M5.Display.fillScreen(RED);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(WHITE);
  M5.Display.setCursor(20, 50);
  M5.Display.print("Shutting Down");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(40, 80);
  M5.Display.print("Goodbye!");
  delay(2000);
  M5.Power.powerOff();
}

void executeMenuOption(int index) {
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(CYAN);
  M5.Display.setCursor(30, 50);

  switch (index) {
    case 0:
      toggleBrightness();
      M5.Display.print("Brightness");
      M5.Display.setCursor(40, 70);
      M5.Display.print(highBrightness ? "HIGH" : "LOW");
      break;
    case 1:
      clearDevices();
      M5.Display.print("Cleared!");
      break;
    case 2:
      shutdownDevice();
      return;
  }

  M5.Display.setTextSize(1);
  delay(1000);
  displayMenuScreen();
  highlightMenuOption(menuIndex);
}

bool checkButtonCombo() {
  unsigned long currentMillis = millis();

  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    if (currentMillis - lastComboCheck > 300) {
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
    M5.Display.setBrightness(204);
    screenDimmed = false;
    displayTrackedDevices();
    return;
  }

  if (paused) {
    if (scrollIndex > 0) {
      scrollIndex--;
      displayTrackedDevices();
    }
  } else {
    paused = true;
    scrollIndex = 0;

    M5.Display.fillScreen(BLACK);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(RED);
    M5.Display.setCursor(50, 50);
    M5.Display.print("PAUSED");
    M5.Display.setTextSize(1);
    M5.Display.setCursor(20, 90);
    M5.Display.setTextColor(WHITE);
    M5.Display.print("Hold B to Resume");
    delay(1500);
    displayTrackedDevices();
  }
}

void handleBtnB() {
  lastButtonPressTime = millis();

  if (screenDimmed) {
    highBrightness = true;
    M5.Display.setBrightness(204);
    screenDimmed = false;
    displayTrackedDevices();
    return;
  }

  if (paused) {
    unsigned long pressStart = millis();

    while (M5.BtnB.isPressed() && (millis() - pressStart < 1000)) {
      M5.update();
      delay(10);
    }

    if (millis() - pressStart >= 1000) {
      paused = false;
      scrollIndex = 0;
      M5.Display.fillScreen(BLACK);
      M5.Display.setTextSize(3);
      M5.Display.setTextColor(GREEN);
      M5.Display.setCursor(30, 50);
      M5.Display.print("RESUMED");
      M5.Display.setTextSize(1);
      delay(800);
      displayTrackedDevices();
    } else {
      int maxScroll = scanningWiFi ? wifiDeviceIndex - 3 : deviceIndex - 3;
      if (maxScroll < 0) maxScroll = 0;
      if (scrollIndex < maxScroll) {
        scrollIndex++;
        displayTrackedDevices();
      }
    }
  } else {
    filterByName = !filterByName;

    M5.Display.fillScreen(BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(30, 50);
    if (filterByName) {
      M5.Display.setTextColor(BLUE);
      M5.Display.print("Named Only");
    } else {
      M5.Display.setTextColor(ORANGE);
      M5.Display.print("Show All");
    }
    M5.Display.setTextSize(1);
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

    while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
      M5.update();
      delay(10);
    }

    while (inMenu) {
      M5.update();
      delay(50);

      if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
        delay(200);
        while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
          M5.update();
          delay(10);
        }
        inMenu = false;
        M5.Display.fillScreen(BLACK);
        displayTrackedDevices();
        break;
      }

      if (M5.BtnA.wasPressed()) {
        menuIndex = (menuIndex + 1) % 3;
        displayMenuScreen();
        highlightMenuOption(menuIndex);
        delay(200);
      }

      if (M5.BtnB.wasPressed()) {
        executeMenuOption(menuIndex);
        delay(200);
      }
    }
  }
}

void loadDeviceData() {
  File file = SPIFFS.open("/devices.txt", FILE_READ);
  if (!file) return;

  deviceIndex = 0;
  while (file.available() && deviceIndex < MAX_DEVICES) {
    String line = file.readStringUntil('\n');
    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);
    int comma3 = line.indexOf(',', comma2 + 1);

    if (comma1 > 0 && comma2 > 0) {
      memset(&trackedDevices[deviceIndex], 0, sizeof(DeviceInfo));

      String addr = line.substring(0, comma1);
      strncpy(trackedDevices[deviceIndex].address, addr.c_str(), 17);
      trackedDevices[deviceIndex].address[17] = '\0';

      trackedDevices[deviceIndex].lastSeen = line.substring(comma1 + 1, comma2).toInt();
      trackedDevices[deviceIndex].totalCount =
          (comma3 > 0) ? line.substring(comma2 + 1, comma3).toInt() : 0;
      trackedDevices[deviceIndex].persistenceScore =
          (comma3 > 0) ? line.substring(comma3 + 1).toFloat() : 0.0f;

      String mfg = getManufacturer(trackedDevices[deviceIndex].address);
      strncpy(trackedDevices[deviceIndex].manufacturer, mfg.c_str(), 30);
      trackedDevices[deviceIndex].manufacturer[30] = '\0';

      trackedDevices[deviceIndex].firstSeen = trackedDevices[deviceIndex].lastSeen;
      trackedDevices[deviceIndex].isSpecial =
          isSpecialMac(trackedDevices[deviceIndex].address);

      deviceIndex++;
    }
  }

  file.close();
}

void loadIgnoreList() {
  File file = SPIFFS.open("/ignore_list.txt", FILE_READ);
  if (!file) return;

  while (file.available()) {
    String mac = file.readStringUntil('\n');
    mac.trim();
    if (mac.length() > 0) {
      ignoreList.insert(mac);
    }
  }
  file.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting setup...");

  M5.begin();
  Serial.println("M5 initialized");

  M5.Display.fillScreen(BLACK);
  M5.Display.setRotation(3);
  M5.Display.setTextColor(GREEN);
  M5.Display.setTextSize(1);
  M5.Display.setBrightness(204);

  Serial.println("Display initialized");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1100);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  Serial.println("BLE initialized");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }

  Serial.println("SPIFFS initialized");

  loadIgnoreList();
  Serial.println("Ignore list loaded");

  loadDeviceData();
  Serial.println("Device data loaded");

  displayStartupMessage();
  Serial.println("Startup message displayed");

  delay(2000);

  displayTrackedDevices();
  Serial.println("Setup complete");
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
    M5.Display.setBrightness(77);
    screenDimmed = true;
  }

  if (currentMillis - lastScanSwitch > SCAN_SWITCH_INTERVAL) {
    scanningWiFi = !scanningWiFi;
    lastScanSwitch = currentMillis;
  }

  unsigned long currentTime = currentMillis / 1000;

  if (scanningWiFi) {
    int n = WiFi.scanNetworks(false, false, false, 300);
    for (int i = 0; i < n; i++) {
      trackWiFiDevice(WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i),
                      WiFi.channel(i), WiFi.encryptionType(i), currentTime);
    }
    WiFi.scanDelete();
  } else {
    BLEScanResults *foundDevicesPtr = pBLEScan->start(2, false);
    if (foundDevicesPtr) {
      bool newTrackerFound = false;

      int count = foundDevicesPtr->getCount();
      for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice device = foundDevicesPtr->getDevice(i);
        String macAddr = device.getAddress().toString().c_str();

        if (ignoreList.find(macAddr) == ignoreList.end() &&
            !isAllowlistedMac(macAddr.c_str())) {
          if (trackDevice(macAddr.c_str(), device.getRSSI(), currentTime,
                          device.getName().c_str())) {
            newTrackerFound = true;
          }
        }
      }

      if (newTrackerFound && deviceIndex > 0) {
        alertUser(trackedDevices[0].isSpecial, trackedDevices[0].name,
                  trackedDevices[0].address, trackedDevices[0].persistenceScore);
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

  delay(50);
}