# PathShield

Advanced BLE and WiFi tracker detection for M5StickC Plus. Detects AirTags, Tiles, and suspicious devices following you.

## Features

- **Dual-Band Scanning**: Alternates WiFi and BLE detection
- **Persistence Scoring**: Multi-factor algorithm reduces false positives
- **Real-Time Alerts**: Visual notifications for detected trackers
- **5000+ MAC Database**: Automatic manufacturer identification

## Installation

### Web Flasher (Recommended)
[Install PathShield](https://lukeswitz.github.io/PathShield/)**

1. Open link in Chrome, Edge, or Opera (not Safari/Firefox)
2. Connect M5StickC Plus via USB-C
3. Click "Deploy Firmware"
4. Select serial port
5. Wait ~2 minutes

### Arduino IDE

1. Install M5StickCPlus library
2. Board: **M5Stick-C-Plus**
3. Partition: **Huge APP (3MB No OTA/1MB SPIFFS)**
4. Upload sketch

## Controls

### Normal Mode
```
Button A:     Pause/Resume scanning
Button B:     Toggle name filter
A+B (hold):   Settings menu
```

### Paused Mode
```
Button A:      Scroll up
Button B:      Scroll down (tap)
Button B hold: Resume (hold 1 second)
```

### Settings Menu
```
Button A:  Navigate options
Button B:  Select option
A+B hold:  Exit menu
```

## Display Guide

### Header
- **Scan Status**: SCAN (green), PAUSE (red), WiFi/BLE mode
- **Device Count**: Total tracked devices or access points
- **[F]**: Filter active (named devices only)

### Color Codes
```
CYAN    = WiFi access points / Normal BLE
BLUE    = Known tracker (special MAC)
RED     = Suspected tracker (persistence ≥ 0.65)
YELLOW  = Manufacturer name
MAGENTA = Divider lines
WHITE   = Detection counts, RSSI
```

### Device Information

**BLE Devices (3 lines):**
```
Line 1: Device name (size 2 font)
Line 2: Manufacturer from MAC lookup
Line 3: Count/RSSI + MAC address
```

**WiFi Devices (3 lines):**
```
Line 1: SSID or "Hidden"
Line 2: Channel + Encryption type
Line 3: Count/RSSI + BSSID (last 8 chars)
```

### Footer
- Shows current page (1-3/23)
- Scroll hint when paused

## Detection Algorithm

### Persistence Scoring (0.0 - 1.0)

**Factor 1: Detection Frequency (0.25 max)**
- Requires minimum 8 detections
- More frequent = higher score

**Factor 2: Time Window Distribution (0.30 max)**
- Tracks across 4 windows: 5/10/15/20 minutes
- Must appear in 3+ windows
- Consistent presence = tracking behavior

**Factor 3: ε-Connectedness (0.25 max)**
- No gaps >3 minutes between detections
- Continuous tracking pattern
- Distinguishes stalking from coincidence

**Factor 4: RSSI Pattern (0.20 max)**
- Signal strength variations
- Movement correlation

**Alert Threshold: ≥ 0.65**



## Customization

### Known Tracker MACs

Edit `specialMacs[]` in PathShield.ino:
```cpp
const char *specialMacs[] = {
  "4C:00:12",  // Apple AirTag
  "C4:AC:05",  // Tile
  "D3:00:FF",  // Tile (additional)
  "E4:5F:01",  // Samsung SmartTag
  "74:5C:4B",  // Samsung SmartTag+
  "EC:81:93",  // Chipolo
  "FF:FF:C0",  // Generic tracker pattern
};
```

### Adjust Sensitivity

**More Sensitive (more alerts):**
```cpp
#define PERSISTENCE_THRESHOLD 0.50  // Lower threshold
#define MIN_DETECTIONS 5            // Fewer detections needed
#define MIN_WINDOWS 2               // Fewer time windows
```

**Less Sensitive (fewer false positives):**
```cpp
#define PERSISTENCE_THRESHOLD 0.75  // Higher threshold
#define MIN_DETECTIONS 12           // More detections needed
#define MIN_WINDOWS 4               // More time windows
```

**Scan Timing:**
```cpp
#define SCAN_SWITCH_INTERVAL 3000   // 3 seconds each (WiFi/BLE)
// Change to 2000 for faster scanning
// Change to 5000 for slower, battery-saving
```

### Display Customization

**Max devices shown:**
```cpp
const int maxDisplay = 3;  // Change to 2 or 4
```

**Color scheme (in displayTrackedDevices):**
```cpp
M5.Lcd.setTextColor(CYAN);     // Change to GREEN, BLUE, etc.
M5.Lcd.drawFastHLine(0, 0, SCREEN_WIDTH, MAGENTA);  // Border color
```

## Troubleshooting

### No Alerts for Known Tracker

**Solution:**
1. Lower threshold temporarily:
```cpp
#define PERSISTENCE_THRESHOLD 0.40
```
2. Check Serial Monitor (115200 baud) for detection counts
3. Verify tracker is powered on and advertising

### Too Many False Positives

**Solution 1: Add to ignore list**
```
1. Note MAC address from display
2. Add to ignore_list.txt
3. Re-upload SPIFFS data
4. Restart device
```

**Solution 2: Raise threshold**
```cpp
#define PERSISTENCE_THRESHOLD 0.75
#define MIN_DETECTIONS 12
```

**Solution 3: Enable name filter**
- Press Button B to show only named devices
- Hides random MAC devices

### Cannot Resume from Pause

Hold Button B for 1 full second (not just tap).

### Device Crashes / Resets

**Reduce memory usage:**
```cpp
#define MAX_DEVICES 50          // Lower from 100
WiFiDeviceInfo wifiDevices[25]; // Lower from 50
```

**Or disable WiFi scanning:**
```cpp
bool scanningWiFi = false;  // Only scan BLE
```

### SPIFFS Upload Failed

**Arduino IDE:**
1. Close Serial Monitor
2. Ensure correct partition scheme: Huge APP
3. Re-install [ESP32 Filesystem Uploader](https://github.com/me-no-dev/arduino-esp32fs-plugin)

**Manual method:**
```bash
# Find SPIFFS offset (usually 0x290000 for Huge APP)
esptool.py --port COM5 write_flash 0x290000 spiffs.bin
```

### Button Not Responding

- Screen may be dimmed (press any button to wake)
- Wait 200ms between presses (debounce)
- For menu: hold both buttons 300ms+

## Technical Specifications
```
Scan Cycle:       WiFi 3s → BLE 3s (alternating)
BLE Range:        ~30-50 meters
WiFi Range:       ~50-100 meters
Max Devices:      100 BLE + 50 WiFi
Auto-Cleanup:     After 5 minutes
Battery Life:     4-6 hours continuous
Persistence Save: Every 60 seconds
Display Refresh:  Every scan cycle
```


## Known Limitations

1. **MAC Randomization**: Modern phones randomize MACs - use name filter
2. **Range Limited**: BLE/WiFi ranges vary by environment
3. **No GPS**: Detects proximity only, not location
4. **Battery**: Continuous dual-band scanning drains battery in 4-6 hours
5. **iOS Privacy**: Cannot always detect randomized/iOS devices

## Credits

Detection algorithms based on:
- [Chasing-Your-Tail-NG](https://github.com/ArgeliusLabs/Chasing-Your-Tail-NG) - Persistence tracking
- [BLE-Doubt](https://arxiv.org/abs/2205.12235) - Topological classification
- [CreepDetector](https://github.com/AlexLynd/CreepDetector) - Original concept

## License

MIT License - Use at your own risk

## Contributing

Issues and pull requests welcome. Test thoroughly before submitting.

## Support

- GitHub Issues: Bug reports and feature requests
- Serial Monitor: Enable debugging (115200 baud)
- [Web Flasher](https://lukeswitz.github.io/PathShield/)