# PathShield

## Overview
PathShield is an advanced anti-tracking tool designed for the M5StickC Plus, leveraging Bluetooth Low Energy (BLE) scanning to detect and identify potential tracking devices around you. With real-time device monitoring, manufacturer identification, and intuitive UI alerts, PathShield provides enhanced privacy and security on the go.

## Features
- Real-Time Scanning: Continuously scans for nearby BLE devices and monitors their signal strength.
- Manufacturer Identification: Uses a comprehensive MAC address prefix database to identify the manufacturer of detected devices.
- Device Tracking and History: Keeps track of detected devices, including signal stability and significant RSSI variations.
- User Interface: Interactive and responsive display with alert animations and device filtering options.
- Special Device Detection: Flags known trackers and important devices based on predefined MAC addresses.

## Installation
1. Clone the Repository:
```sh
git clone https://github.com/yourusername/PathShield.git
```

2. Add Libraries

- Ensure you have the required libraries:
  `M5StickCPlus, BLEDevice, BLEUtils, BLEScan`

3. Copy MacPrefixes.h to your Arduino libraries project directory

## Usage

- Power On: Turn on your M5StickC Plus.
- Start Scanning: The device will automatically start scanning for nearby BLE devices and display them on the screen.

**Pause/Resume Scanning:**
- Press Button A to pause or resume scanning.
- When paused, the screen will display "Paused" with a red indicator.
- When resumed, the screen will display "Resumed" with a green indicator.

**Filter by Device Name:**
- Press Button B to toggle the name filter.
- When the filter is ON, the screen will display "Filter: ON" with a blue indicator.
- When the filter is OFF, the screen will display "Filter: OFF" with an orange indicator.

**Alerts:**
- If a new tracking device is detected, the screen will flash red and display "New Tracker Detected!" for 2 seconds.

**View Tracked Devices:**
- Detected devices and their information will be listed on the screen, including name, MAC address, manufacturer, and detection count.
- Devices flagged as special will be highlighted in red.

**Monitor Screen:**
- The main screen shows the list of detected devices, updating in real-time as new devices are found and old devices are removed after a set detection window.
