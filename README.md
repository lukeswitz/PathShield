<div align="center">
<img height="500" alt="image" src="https://github.com/user-attachments/assets/0dac6a9f-32a8-4b05-b6a1-b59fc3762f51" />

PathShield is an RF awareness tool for M5StickC-Plus (v1 & v2). It uses BLE/WiFI scanning to detect devices, alerting on those following you.
</div>


---

> [!CAUTION]
> **ETHICAL USE ONLY**
> Users are solely responsible for: Compliance with all applicable laws. Respecting reasonable expectations of privacy


## Table of Contents

1. [Features](#features)
2. [Installation](#installation)
3. [Controls](#controls)
4. [Display Guide](#display-guide)
5. [Detection Algorithm](#detection-algorithm)
6. [Customization](#customization)
7. [Troubleshooting](#troubleshooting)
8. [Known Limitations](#known-limitations)
9. [Credits](#credits)
10. [License](#license)


## Features

- **Dual-Band Scanning**: Alternates WiFi and BLE detection, displays MAC, vendor, SSID, channel, hit count & RSSI.
- **Persistence Scoring**: Multi-factor algorithm reduces false positives. Allowlist for known devices.
- **Real-Time Alerts**: Visual notifications for detected trackers and user specified MAC targets
- **24,500+ MAC Database**: Offline manufacturer identification (WiFi/BLE + consumer/IoT devices)
- **Efficient Updates**: Only redraws display when data changes (reduces power draw)

> [!TIP]
> Modify `allowlistMacs` to ignore known devices. Change the `specialMacs` to your own target devices (default detects Flock and Axon Taser cameras).

## Installation

### Web Flasher
[Install PathShield](https://lukeswitz.github.io/PathShield/)

1. Open link in Chrome, Edge, or Opera (not Safari/Firefox)
2. Connect device via USB-C
3. Click "Deploy Firmware"
4. Select serial port, click "Connect"
5. Select "Install PathShield"
6. Optional: Choose "Erase device" (to clear artifact SPIFFS data)
7. Select "Next" > Install
8. Wait ~2 minutes for it to complete

### From Source w/Arduino IDE

1. Install M5Unified library
2. Board: **M5Stick-C-Plus**
3. Partition: **Huge APP (3MB No OTA/1MB SPIFFS)**
4. Upload sketch


## Controls

### Normal Scanning Mode
```
Button A:   Pause scanning
Button B:   Toggle name filter
A+B (hold): Settings menu
```

### Paused 
```
Button A:      Scroll up
Button B:      Scroll down (tap)
Button B hold: Resume (hold 1 second)
```

### Settings Menu
```
Button A:  Navigate options (up/down)
Button B:  Select option
A+B hold:  Exit menu
```

**Available Settings:**
- **Brightness**: Low/High (saves battery on low brightness)
- **Clear Devices**: Clears all tracked devices from memory
- **Display Timeout**: How long before screen turns off when idle (10-300 seconds)
- **Shutdown Device**: Power off the device

## Display Guide

### Top Status Bar
- **Scan Mode**: `SCAN` (green = actively scanning), `PAUSE` (red = paused)
- **WiFi/BLE Indicator**: Current scan mode (WiFi or Bluetooth)
- **Battery Bar**: Device battery percentage (0-100%)
- **Memory Bar**: Available RAM in KB (green = good, yellow = warning, red = critical)

### Device List
Each device shows:
```
Device Name (BLE) or SSID (WiFi)
Manufacturer (identified from MAC)
Detection Count + Signal Strength (RSSI)
```

### Color Codes
```
CYAN    = WiFi networks / Normal Bluetooth devices
ORANGE  = User-defined tracker (special MAC)
RED     = Suspected tracker detected (high persistence score)
YELLOW  = Manufacturer name
GREEN   = Scan active, status messages
```

### Filter Mode
- Press **Button B** to toggle between "Show All" (all devices) and "Named Only" (only named devices)
- Useful for cutting through noise when there are many unnamed devices

### Footer
- **Page counter**: Shows which page you're viewing (e.g., "1-3/23")
- **Scroll hint**: When paused, shows navigation instructions

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
  // Apple OUIs (AirTags use rotating addresses from Apple's OUI space)
  "AC:DE:48",  // Apple Inc.
  "F0:98:9D",  // Apple Inc.
  "BC:92:6B",  // Apple Inc.
  
  // Tile
  "C4:AC:05",  // Tile Inc.
  "E0:00:00",  // Tile Inc. (some models)
  
  // Samsung SmartTag
  "E4:5F:01",  // Samsung Electronics
  "74:5C:4B",  // Samsung Electronics
  "E8:50:8B",  // Samsung Electronics (additional)
  
  // Chipolo
  "EC:81:93",  // Chipolo d.o.o.
};
```

### Allowlist (Trusted Devices)

Add your own trusted devices to `allowlistMacs[]` to prevent false alerts:

```cpp
const char *allowlistMacs[] = {
  "AA:BB:CC",  // Your trusted device 1
  "DD:EE:FF",  // Your trusted device 2
  // Add MAC prefixes of devices you own
};
```

Allowlisted devices are completely ignored during scanning and will never trigger tracker alerts.

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
M5.Display.setTextColor(CYAN);     // Change to GREEN, BLUE, etc.
M5.Display.drawFastHLine(0, 0, SCREEN_WIDTH, MAGENTA);  // Border color
```

## Troubleshooting

> Tested with M5stickCPlus-v1 and v2. Support for more devices can be requested by opening a ticket

### No Alerts for Known Tracker

**Solution:**
1. Lower threshold temporarily:
```cpp
#define PERSISTENCE_THRESHOLD 0.40
```
2. Check Serial Monitor (115200 baud) for detection counts
3. Verify tracker is powered on and advertising

### Too Many False Positives

**Quick fix: Use name filter**
- Press **Button B** to show only named devices
- Hides random MAC addresses and noise

**Persistent false positives: Add to allowlist**
1. Note the MAC address from the display
2. Add to `allowlistMacs[]` in PathShield.ino
3. Re-upload and restart

**Fine-tune sensitivity (advanced)**
```cpp
#define PERSISTENCE_THRESHOLD 0.75  // Raise to be more strict
#define MIN_DETECTIONS 12           // Require more detections
```

### Cannot Resume from Pause

Hold Button B for 1 full second (not just tap).

### Device Crashes / Resets

**First try:** Lower sensitivity to reduce memory load
```cpp
#define MAX_DEVICES 50          // Reduce tracked devices
#define MAX_WIFI_DEVICES 25     // Reduce WiFi scanning
```

**Still crashing?** Disable WiFi scanning to use BLE only
```cpp
bool scanningWiFi = false;  // Only scan Bluetooth
```

Watch the memory bar on screen—red means critically low.

### SPIFFS Upload Failed

1. Close Serial Monitor (it blocks uploads)
2. Verify board: **M5Stick-C-Plus**
3. Verify partition: **Huge APP (3MB No OTA/1MB SPIFFS)**
4. Try again

If still failing, use the [Web Flasher](https://lukeswitz.github.io/PathShield/) instead.

### Button Not Responding

- Screen may be dimmed (press any button to wake)
- Wait 200ms between presses (debounce)
- For menu: hold both buttons 300ms+


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

MIT License - Use at your own risk. Developers provide no warranty and accept no liability for unlawful or
unethical use. Review local regulations before deployment.

## Contributing

Issues and pull requests welcome. Test thoroughly before submitting.

## Support

- GitHub Issues: Bug reports and feature requests
- Serial Monitor: Enable debugging (115200 baud)
- [Web Flasher](https://lukeswitz.github.io/PathShield/)
