#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "MacPrefixes.h"
#include <algorithm>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
#define DEFAULT_SCREEN_TIMEOUT 30000

// Memory constants, TWEAK THESE FOR RF DENSITY
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

#define MIN_DETECTIONS 12
#define MIN_WINDOWS 3
#define PERSISTENCE_THRESHOLD 0.75
#define RSSI_STABILITY_THRESHOLD 10
#define RSSI_VARIATION_THRESHOLD 15
#define EPSILON_CONNECTED_GAP 180
#define MIN_RSSI_RANGE 12

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
unsigned long lastButtonPressTime = 0;
unsigned long lastActivityTime = 0;
bool screenOn = true;
unsigned long screenTimeoutMs = DEFAULT_SCREEN_TIMEOUT;
volatile bool deviceDataChanged = false;
static uint32_t lastStateHash = 0;

// Watchdog status
bool loopWatchdogActive = false;

bool alertActive = false;
unsigned long alertStartTime = 0;
const unsigned long ALERT_DURATION = 5000;

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

// IGNORE LIST: Add MAC prefixes of YOUR devices here to ignore them
// Example: const char *allowlistMacs[] = {"AA:BB:CC", "DD:EE:FF", "11:22:33"};
// Leave as {""} to track all devices
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
  int minRssi;
  int maxRssi;
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
int scrollIndex = 0;

// FreeRTOS multi-core support
SemaphoreHandle_t deviceMutex = NULL;
TaskHandle_t scanTaskHandle = NULL;
volatile bool scanTaskRunning = true;

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

  // Calculate RSSI range (difference between max and min)
  int rssiRange = device.maxRssi - device.minRssi;

  if (rssiRange < MIN_RSSI_RANGE) {
    return 0.0f;
  }

  if (device.totalCount >= MIN_DETECTIONS) {
    score += 0.20f * (float)std::min(device.totalCount, 30) / 30.0f;
  } else {
    return 0.0f;
  }

  int windowsActive = 0;
  for (int i = 0; i < 4; i++) {
    if (device.windows[i].detections > 0) windowsActive++;
  }
  if (windowsActive >= MIN_WINDOWS) {
    score += 0.25f * (float)windowsActive / 4.0f;
  } else {
    return 0.0f;
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
    }
  }

  if (device.variationCount >= 3) { // sketchy variance
    score += 0.30f * (float)std::min(device.variationCount, 10) / 10.0f;
  }

  return score;
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

      // Track RSSI range for movement detection
      if (rssi < trackedDevices[i].minRssi) trackedDevices[i].minRssi = rssi;
      if (rssi > trackedDevices[i].maxRssi) trackedDevices[i].maxRssi = rssi;

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
    trackedDevices[0].minRssi = rssi;
    trackedDevices[0].maxRssi = rssi;
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

  screenOn = true;
  lastActivityTime = millis();
  M5.Display.setBrightness(204);

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

  // Force display reset after alert dismissal to prevent race condition
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(1);
  lastStateHash = 0;  // Force redraw on next update
  screenOn = true;
  lastActivityTime = millis();
  M5.Display.setBrightness(highBrightness ? 204 : 77);
}

void showFeedback(const char* msg, uint16_t color, const char* sub = NULL) {
  M5.Display.fillScreen(BLACK);
  for (int i = 0; i < 15; i++) {
    M5.Display.drawFastHLine(random(0, SCREEN_WIDTH), random(0, SCREEN_HEIGHT),
                             random(5, 40), random(0x0000, 0x1111));
  }
  M5.Display.drawFastHLine(0, 35, SCREEN_WIDTH, CYAN);
  M5.Display.drawFastHLine(0, 36, SCREEN_WIDTH, CYAN);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(color);
  int xPos = (SCREEN_WIDTH - strlen(msg) * 18) / 2;
  M5.Display.setCursor(xPos, 50);
  M5.Display.print(msg);
  if (sub) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(YELLOW);
    M5.Display.setCursor((SCREEN_WIDTH - strlen(sub) * 6) / 2, 80);
    M5.Display.print(sub);
  }
  M5.Display.drawFastHLine(0, 95, SCREEN_WIDTH, MAGENTA);
  M5.Display.drawFastHLine(0, 96, SCREEN_WIDTH, MAGENTA);
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

  static float lastValidBatVoltage = 3.7f;
  float batVoltage = M5.Power.getBatteryVoltage() / 1000.0f;

  // Filter out obviously bad readings (sensor errors), but allow real low battery
  if (batVoltage > 2.0f && batVoltage < 4.5f) {
    lastValidBatVoltage = batVoltage;
  } else {
    batVoltage = lastValidBatVoltage;
  }

  // Critical battery check - force shutdown if critically low
  if (batVoltage < 3.0f && batVoltage > 2.0f) {
    M5.Display.fillScreen(RED);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(10, 50);
    M5.Display.print("LOW BATTERY!");
    M5.Display.setCursor(20, 80);
    M5.Display.print("SHUTTING DOWN");
    delay(3000);
    M5.Power.powerOff();
  }

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
  M5.Display.print("BATT:");

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
  M5.Display.print("MEM:");

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

// Generate hashed display state
uint32_t getDisplayStateHash() {
  uint32_t hash = 0;
  
  hash = deviceIndex * 31 + wifiDeviceIndex;
  hash = hash * 31 + scrollIndex;
  hash = hash * 31 + (scanningWiFi ? 1 : 0);
  hash = hash * 31 + (paused ? 1 : 0);
  hash = hash * 31 + (filterByName ? 1 : 0);

  if (scanningWiFi) {
    for (int i = 0; i < wifiDeviceIndex; i++) {
      hash = hash * 31 + (uint32_t)wifiDevices[i].rssi;
      hash = hash * 31 + wifiDevices[i].detectionCount;
      hash = hash * 31 + wifiDevices[i].channel;
      hash = hash * 31 + wifiDevices[i].encryptionType;
      for (int j = 0; j < strlen(wifiDevices[i].ssid); j++) {
        hash = hash * 31 + wifiDevices[i].ssid[j];
      }
    }
  } else {
    for (int i = 0; i < deviceIndex; i++) {
      hash = hash * 31 + trackedDevices[i].totalCount;
      hash = hash * 31 + (uint32_t)trackedDevices[i].lastRssi;
      hash = hash * 31 + (trackedDevices[i].detected ? 1 : 0);
      hash = hash * 31 + (trackedDevices[i].isSpecial ? 1 : 0);
      for (int j = 0; j < strlen(trackedDevices[i].name); j++) {
        hash = hash * 31 + trackedDevices[i].name[j];
      }
      for (int j = 0; j < strlen(trackedDevices[i].manufacturer); j++) {
        hash = hash * 31 + trackedDevices[i].manufacturer[j];
      }
    }
  }

  return hash;
}

void displayTrackedDevices() {
  static unsigned long lastRender = 0;
  unsigned long now = millis();

  if (now - lastRender < 500) {
    return;
  }

  if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  uint32_t currentHash = getDisplayStateHash();
  if (currentHash == lastStateHash) {
    xSemaphoreGive(deviceMutex);
    return;
  }

  // Actually have data to change, go on
  lastStateHash = currentHash;
  lastRender = now;

  xSemaphoreGive(deviceMutex);

  M5.Display.fillScreen(BLACK);
  drawTopBar();

  if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

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

      M5.Display.setTextColor(WIFI_NAME_COLOR);
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

      if (trackedDevices[i].isSpecial) {
        M5.Display.setTextColor(ORANGE);
      } else if (trackedDevices[i].detected) {
        M5.Display.setTextColor(RED);
      } else {
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

    char countStr[20];
    int showing = std::min(maxDisplay, totalItems - scrollIndex);
    snprintf(countStr, sizeof(countStr), "%d-%d/%d", scrollIndex + 1,
             scrollIndex + showing, totalItems);

    int textWidth = strlen(countStr) * 6;
    int xPos = SCREEN_WIDTH - textWidth - 4;
    M5.Display.setCursor(xPos, 124);
    M5.Display.print(countStr);

    if (paused && totalItems > maxDisplay) {
      M5.Display.setCursor(80, 124);
      M5.Display.setTextColor(GREEN);
      M5.Display.print("A/B:Scroll");
    }
  }

  xSemaphoreGive(deviceMutex);
}

void displayMenuScreen() {
  static unsigned long lastRender = 0;
  unsigned long now = millis();
  
  if (now - lastRender < 500) {
    return;
  }
  lastRender = now;

  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(2, 2);
  M5.Display.setTextColor(GREEN);
  M5.Display.setTextSize(1);
  M5.Display.print("SETTINGS");

  M5.Display.drawLine(0, 12, SCREEN_WIDTH, 12, DARKGREY);

  int y = 16;

  M5.Display.setTextColor(WHITE);
  M5.Display.setCursor(2, y);

  static float lastValidBatVoltage = 3.7f;
  float batVoltage = M5.Power.getBatteryVoltage() / 1000.0f;

  // Filter out obviously bad readings (sensor errors), but allow real low battery
  if (batVoltage > 2.0f && batVoltage < 4.5f) {
    lastValidBatVoltage = batVoltage;
  } else {
    batVoltage = lastValidBatVoltage;
  }

  int batPercent = (int)((batVoltage - 3.0f) / 1.2f * 100.0f);
  if (batPercent > 100) batPercent = 100;
  if (batPercent < 0) batPercent = 0;
  M5.Display.print("Bat:");
  M5.Display.print(batPercent);
  M5.Display.print("% Bright:");
  M5.Display.print(highBrightness ? "Hi" : "Lo");
  y += 12;

  M5.Display.setCursor(2, y);
  M5.Display.print("Timeout:");
  M5.Display.print(screenTimeoutMs / 1000);
  M5.Display.print("s Tracked:");
  M5.Display.print(deviceIndex);
  y += 12;

  M5.Display.setCursor(2, y);
  M5.Display.print("RAM:");
  M5.Display.print(ESP.getFreeHeap() / 1024);
  M5.Display.print("KB");
  y += 16;

  M5.Display.drawLine(0, y, SCREEN_WIDTH, y, DARKGREY);
  y += 4;

  menuBaseY = y;

  M5.Display.setTextColor(CYAN);

  M5.Display.setCursor(10, y);
  M5.Display.print("Toggle Brightness");
  y += 11;

  M5.Display.setCursor(10, y);
  M5.Display.print("Set Screen Timeout");
  y += 11;

  M5.Display.setCursor(10, y);
  M5.Display.print("Clear Devices");
  y += 11;

  M5.Display.setCursor(10, y);
  M5.Display.print("Shutdown");
  y += 12;

  M5.Display.drawLine(0, y, SCREEN_WIDTH, y, DARKGREY);

  M5.Display.setTextColor(YELLOW);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(2, y + 3);
  M5.Display.print("A:Nav B:Sel A+B:Exit");
}

void highlightMenuOption(int index) {
  for (int i = 0; i < 4; i++) {
    M5.Display.setCursor(2, menuBaseY + (i * 11));
    M5.Display.setTextColor(BLACK);
    M5.Display.print(">");
  }

  M5.Display.setCursor(2, menuBaseY + (index * 11));
  M5.Display.setTextColor(YELLOW);
  M5.Display.print(">");
}

void setScreenTimeout() {
  int timeoutOptions[] = {10000, 15000, 30000, 60000, 120000, 300000};
  int optionCount = 6;
  int selected = 0;
  
  for (int i = 0; i < optionCount; i++) {
    if (timeoutOptions[i] == screenTimeoutMs) {
      selected = i;
      break;
    }
  }

  bool settingTimeout = true;
  unsigned long lastRender = 0;
  
  while (settingTimeout) {
    unsigned long now = millis();
    if (now - lastRender >= 200) {
      lastRender = now;
      
      M5.Display.fillScreen(BLACK);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(GREEN);
      M5.Display.setCursor(10, 10);
      M5.Display.print("Screen Timeout");
      
      M5.Display.drawLine(0, 20, SCREEN_WIDTH, 20, DARKGREY);

      int y = 30;
      for (int i = 0; i < optionCount; i++) {
        if (i == selected) {
          M5.Display.setTextColor(YELLOW);
          M5.Display.setCursor(5, y);
          M5.Display.print(">");
        } else {
          M5.Display.setTextColor(WHITE);
          M5.Display.setCursor(10, y);
        }
        M5.Display.print(timeoutOptions[i] / 1000);
        M5.Display.print("s");
        y += 12;
      }

      M5.Display.setTextColor(CYAN);
      M5.Display.setCursor(10, 110);
      M5.Display.print("A:Up B:Select");
    }

    M5.update();
    if (M5.BtnA.wasPressed()) {
      selected = (selected - 1 + optionCount) % optionCount;
      delay(200);
    }
    if (M5.BtnB.wasPressed()) {
      screenTimeoutMs = timeoutOptions[selected];
      settingTimeout = false;
      delay(200);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void toggleBrightness() {
  highBrightness = !highBrightness;
  M5.Display.setBrightness(highBrightness ? 204 : 77);
  lastButtonPressTime = millis();
}

void saveDeviceData() {
  if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return;
  }

  File file = SPIFFS.open("/devices.txt", FILE_WRITE);
  if (!file) {
    xSemaphoreGive(deviceMutex);
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
  xSemaphoreGive(deviceMutex);
}

void clearDevices() {
  if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return;
  }

  deviceIndex = 0;
  scrollIndex = 0;
  SPIFFS.remove("/devices.txt");

  xSemaphoreGive(deviceMutex);
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
  switch (index) {
    case 0:
      toggleBrightness();
      showFeedback(highBrightness ? "BRIGHT" : "DIM", CYAN);
      delay(1000);
      break;
    case 1:
      cycleScreenTimeout();
      break;
    case 2:
      clearDevices();
      showFeedback("CLEARED", GREEN);
      delay(1000);
      break;
    case 3:
      shutdownDevice();
      return;
  }

  inMenu = true;
  displayMenuScreen();
  highlightMenuOption(menuIndex);
}

void cycleScreenTimeout() {
  int timeoutOptions[] = {10000, 15000, 30000, 60000, 120000, 300000};
  int optionCount = 6;
  int currentIdx = 0;
  
  for (int i = 0; i < optionCount; i++) {
    if (timeoutOptions[i] == screenTimeoutMs) {
      currentIdx = i;
      break;
    }
  }
  
  currentIdx = (currentIdx + 1) % optionCount;
  screenTimeoutMs = timeoutOptions[currentIdx];

  char timeStr[20];
  snprintf(timeStr, 20, "%ds TIMEOUT", screenTimeoutMs / 1000);
  showFeedback(timeStr, CYAN);
  delay(1000);
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
  lastActivityTime = millis();

  if (!screenOn) {
    screenOn = true;
    M5.Display.setBrightness(highBrightness ? 204 : 77);
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
    showFeedback("PAUSED", RED, "Hold B to Resume");
    delay(1500);
    displayTrackedDevices();
  }
}

void handleBtnB() {
  lastButtonPressTime = millis();

  if (screenDimmed) {
    screenOn = true;
    highBrightness = true;
    M5.Display.setBrightness(204);
    screenDimmed = false;
    lastActivityTime = millis();
    displayTrackedDevices();
    return;
  }

  if (!screenOn) {
    screenOn = true;
    M5.Display.setBrightness(highBrightness ? 204 : 77);
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
      showFeedback("RESUMED", GREEN);
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
    showFeedback(filterByName ? "NAMED ONLY" : "SHOW ALL",
                 filterByName ? CYAN : ORANGE);
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
        menuIndex = (menuIndex + 1) % 4;
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

// SCANNING TASK - Runs on Core 0
void scanTask(void *parameter) {
  Serial.println("scanTask started on Core 0");

  // Add this task to watchdog
  esp_err_t wdt_add_result = esp_task_wdt_add(NULL);
  if (wdt_add_result == ESP_OK) {
    Serial.println("scanTask added to watchdog");
  } else {
    Serial.print("scanTask watchdog add failed: ");
    Serial.println(wdt_add_result);
  }

  vTaskDelay(1000 / portTICK_PERIOD_MS);
  Serial.println("scanTask beginning scans");

  unsigned long lastScanSwitch = 0;
  const unsigned long SCAN_SWITCH_INTERVAL = 3000;
  bool localScanningWiFi = true;

  while (scanTaskRunning) {
    if (paused) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    unsigned long currentMillis = millis();
    unsigned long currentTime = currentMillis / 1000;

    // Switch between WiFi and BLE scanning
    if (currentMillis - lastScanSwitch > SCAN_SWITCH_INTERVAL) {
      localScanningWiFi = !localScanningWiFi;
      lastScanSwitch = currentMillis;

      // Update global flag with mutex (with timeout to prevent deadlock)
      if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        scanningWiFi = localScanningWiFi;
        xSemaphoreGive(deviceMutex);
      }
    }

    if (localScanningWiFi) {
      // WiFi Scan (blocking)
      int n = WiFi.scanNetworks(false, false, false, 300);

      if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        for (int i = 0; i < n; i++) {
          trackWiFiDevice(WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(),
                          WiFi.RSSI(i), WiFi.channel(i), WiFi.encryptionType(i),
                          currentTime);
          vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        xSemaphoreGive(deviceMutex);
      }
      WiFi.scanDelete();

    } else {
      // BLE Scan (blocking)
      BLEScanResults *foundDevicesPtr = pBLEScan->start(2, false);

      if (foundDevicesPtr) {
        bool newTrackerFound = false;
        int alertDeviceIdx = -1;

        if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
          int count = foundDevicesPtr->getCount();
          for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice device = foundDevicesPtr->getDevice(i);
            String macAddr = device.getAddress().toString().c_str();

            if (!isAllowlistedMac(macAddr.c_str())) {
              if (trackDevice(macAddr.c_str(), device.getRSSI(), currentTime,
                              device.getName().c_str())) {
                newTrackerFound = true;
                alertDeviceIdx = 0;
              }
            }
            vTaskDelay(1 / portTICK_PERIOD_MS);
          }
          xSemaphoreGive(deviceMutex);
        }

        if (newTrackerFound && alertDeviceIdx >= 0) {
          bool isSpecial = false;
          char alertName[21];
          char alertAddr[18];
          float alertScore = 0.0f;

          if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (deviceIndex > 0) {
              isSpecial = trackedDevices[0].isSpecial;
              strncpy(alertName, trackedDevices[0].name, 20);
              alertName[20] = '\0';
              strncpy(alertAddr, trackedDevices[0].address, 17);
              alertAddr[17] = '\0';
              alertScore = trackedDevices[0].persistenceScore;
            }
            xSemaphoreGive(deviceMutex);
          }

          if (deviceIndex > 0) {
            alertUser(isSpecial, alertName, alertAddr, alertScore);
          }
        }

        pBLEScan->clearResults();
      }
    }

    // Clean up old entries periodically
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      removeOldEntries(currentTime);
      removeOldWiFiEntries(currentTime);
      xSemaphoreGive(deviceMutex);
    }

    // Feed watchdog timer if good to eat
    if (wdt_add_result == ESP_OK) {
      esp_task_wdt_reset();
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }

  Serial.println("scanTask terminated");
  if (wdt_add_result == ESP_OK) {
    esp_task_wdt_delete(NULL);
  }
  vTaskDelete(NULL);
}

void setup() {
  esp_task_wdt_deinit();

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 10000,
    .trigger_panic = true
  };
  esp_err_t wdt_result = esp_task_wdt_init(&wdt_config);

  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting setup...");

  if (wdt_result == ESP_OK) {
    Serial.println("Watchdog timer initialized");
  } else {
    Serial.print("Watchdog init failed: ");
    Serial.println(wdt_result);
  }

  M5.begin();
  Serial.println("M5 initialized");
  delay(100);

  M5.Display.fillScreen(BLACK);
  M5.Display.setRotation(3);
  M5.Display.setTextColor(GREEN);
  M5.Display.setTextSize(1);
  M5.Display.setBrightness(204);

  delay(100);
  Serial.println("Display initialized");

  displayStartupMessage();
  Serial.println("Startup message displayed");

  delay(100);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1100);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  Serial.println("BLE initialized");
  delay(200);

  if (!SPIFFS.begin(false)) {
    Serial.println("SPIFFS corrupted, formatting...");

    M5.Display.fillScreen(BLACK);
    for (int i = 0; i < 15; i++) {
      M5.Display.drawFastHLine(random(0, SCREEN_WIDTH), random(0, SCREEN_HEIGHT),
                               random(5, 40), random(0x0000, 0x1111));
    }

    M5.Display.drawFastHLine(0, 25, SCREEN_WIDTH, MAGENTA);
    M5.Display.drawFastHLine(0, 26, SCREEN_WIDTH, MAGENTA);

    M5.Display.setTextSize(3);
    M5.Display.setTextColor(YELLOW);
    M5.Display.setCursor(25, 35);
    M5.Display.print("SPIFFS");

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(ORANGE);
    M5.Display.setCursor(55, 60);
    M5.Display.print("FORMATTING...");

    M5.Display.drawFastHLine(0, 75, SCREEN_WIDTH, CYAN);
    M5.Display.drawFastHLine(0, 76, SCREEN_WIDTH, CYAN);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(DARKGREY);
    M5.Display.setCursor(35, 90);
    M5.Display.print("Please wait...");

    SPIFFS.format();
    delay(100);

    if (!SPIFFS.begin(false)) {
      Serial.println("SPIFFS mount failed critically");
      M5.Display.fillScreen(BLACK);

      for (int i = 0; i < 20; i++) {
        M5.Display.drawFastHLine(random(0, SCREEN_WIDTH), random(0, SCREEN_HEIGHT),
                                 random(5, 40), random(0x0000, 0x1111));
      }

      M5.Display.drawFastHLine(0, 25, SCREEN_WIDTH, RED);
      M5.Display.drawFastHLine(0, 26, SCREEN_WIDTH, RED);

      M5.Display.setTextSize(3);
      M5.Display.setTextColor(RED);
      M5.Display.setCursor(40, 40);
      M5.Display.print("ERROR");

      M5.Display.setTextSize(1);
      M5.Display.setTextColor(YELLOW);
      M5.Display.setCursor(30, 70);
      M5.Display.print("SPIFFS FAILED");

      M5.Display.drawFastHLine(0, 85, SCREEN_WIDTH, RED);
      M5.Display.drawFastHLine(0, 86, SCREEN_WIDTH, RED);

      M5.Display.setTextColor(DARKGREY);
      M5.Display.setCursor(50, 100);
      M5.Display.print("Reboot needed");

      while(1) delay(1000);
      return;
    }

    // Show success message
    M5.Display.fillScreen(BLACK);
    for (int i = 0; i < 15; i++) {
      M5.Display.drawFastHLine(random(0, SCREEN_WIDTH), random(0, SCREEN_HEIGHT),
                               random(5, 40), random(0x0000, 0x1111));
    }
    M5.Display.drawFastHLine(0, 35, SCREEN_WIDTH, GREEN);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(GREEN);
    M5.Display.setCursor(35, 50);
    M5.Display.print("SUCCESS!");
    M5.Display.drawFastHLine(0, 70, SCREEN_WIDTH, GREEN);
    delay(800);
  }

  Serial.println("SPIFFS initialized");
  
  deviceMutex = xSemaphoreCreateMutex();
  if (deviceMutex == NULL) {
    Serial.println("ERROR: Failed to create device mutex!");
    while (1) { delay(300); }
  }
  Serial.println("Device mutex created");
  
  delay(1000);

  M5.Display.fillScreen(BLACK);
  drawTopBar();

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(DARKGREY);
  M5.Display.setCursor(60, 60);
  M5.Display.print("Starting scans...");
  Serial.println("Initial display ready");


  xTaskCreatePinnedToCore(
    scanTask,          // Task function
    "ScanTask",        // Task name
    8192,              // Stack size (bytes)
    NULL,              // Parameters
    1,                 // Priority 
    &scanTaskHandle,   // Task handle
    0                  // Core 0 (Core 1 is for loop)
  );
  Serial.println("Scanning task started on Core 0");

  delay(100);

  // Initialize activity timer to prevent immediate screen timeout
  lastActivityTime = millis();
  lastButtonPressTime = millis();

  // Add loop task to watchdog after everything is initialized
  esp_err_t loop_wdt_result = esp_task_wdt_add(NULL);
  if (loop_wdt_result == ESP_OK) {
    Serial.println("Loop task added to watchdog");
    loopWatchdogActive = true;
  } else {
    Serial.print("Loop task watchdog add failed: ");
    Serial.println(loop_wdt_result);
    loopWatchdogActive = false;
  }

  Serial.println("PathShield setup complete");
}

void loop() {
  // Feed watchdog timer FIRST - before any early returns
  if (loopWatchdogActive) {
    esp_task_wdt_reset();
  }

  unsigned long currentMillis = millis();
  static unsigned long lastDisplayUpdate = 0;
  static bool firstRun = true;
  static unsigned long lastMemoryCheck = 0;
  const unsigned long DISPLAY_UPDATE_INTERVAL = 1000;
  const unsigned long MEMORY_CHECK_INTERVAL = 5000;

  M5.update();

  // Memory safety check every 5 seconds
  if (currentMillis - lastMemoryCheck > MEMORY_CHECK_INTERVAL) {
    lastMemoryCheck = currentMillis;
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t freeKB = freeHeap / 1024;

    // Critical memory level - force cleanup
    if (freeKB < 10) {
      Serial.println("CRITICAL MEMORY! Forcing cleanup...");
      if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        // Clear oldest half of devices
        int toClear = deviceIndex / 2;
        for (int i = deviceIndex - toClear; i < deviceIndex; i++) {
          if (!trackedDevices[i].alertTriggered && !trackedDevices[i].isSpecial) {
            deviceIndex = i;
            break;
          }
        }
        xSemaphoreGive(deviceMutex);
      }
    }
    // Low memory warning
    else if (freeKB < 20) {
      Serial.print("Low memory warning: ");
      Serial.print(freeKB);
      Serial.println(" KB free");
    }
  }

  if (firstRun) {
      M5.Display.setBrightness(highBrightness ? 204 : 77);
      screenOn = true;

      delay(200);
      displayTrackedDevices();
      lastDisplayUpdate = currentMillis;
      firstRun = false;
    }

    bool btnA = M5.BtnA.wasPressed();
    bool btnB = M5.BtnB.wasPressed();

    if ((btnA || btnB) && !screenOn) {
    screenOn = true;
    lastActivityTime = currentMillis;
    lastButtonPressTime = currentMillis;
    M5.Display.setBrightness(highBrightness ? 204 : 77);
    screenDimmed = false;
    lastStateHash = 0;
    displayTrackedDevices();
    lastDisplayUpdate = currentMillis;
    return;
  }

  if (btnA || btnB) {
    lastActivityTime = currentMillis;
    lastButtonPressTime = currentMillis;
  }

  if (checkButtonCombo()) {
    handleButtonCombination();
    return;
  }

  if (!inMenu) {
    if (btnA && (currentMillis - lastBtnAPress > DEBOUNCE_DELAY)) {
      lastBtnAPress = currentMillis;
      handleBtnA();
      lastDisplayUpdate = currentMillis;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      return;
    }

    if (btnB && (currentMillis - lastBtnBPress > DEBOUNCE_DELAY)) {
      lastBtnBPress = currentMillis;
      handleBtnB();
      lastDisplayUpdate = currentMillis;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      return;
    }
  }

  // Dim screen at 75% of timeout (or 5 seconds before timeout, whichever is sooner)
  unsigned long dimThreshold = min(screenTimeoutMs * 3 / 4, screenTimeoutMs - 5000);
  if (dimThreshold < 5000) dimThreshold = screenTimeoutMs / 2; // For very short timeouts

  if (screenOn && !screenDimmed && highBrightness &&
      currentMillis - lastActivityTime > dimThreshold) {
    highBrightness = false;
    M5.Display.setBrightness(77);
    screenDimmed = true;
  }

  // Turn off screen after full timeout
  if (screenOn && currentMillis - lastActivityTime > screenTimeoutMs) {
    screenOn = false;
    M5.Display.setBrightness(0);
  }

  // Refresh display periodically
  if (screenOn && currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    displayTrackedDevices();
    lastDisplayUpdate = currentMillis;
  }

  // Paused or screen off
  if (paused || !screenOn) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  } else {
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }

  static unsigned long lastSaveTime = 0;
  if (currentMillis - lastSaveTime > 60000) {
    saveDeviceData();
    lastSaveTime = currentMillis;
  }
}