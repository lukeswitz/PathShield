# PathShield
![image](https://github.com/lukeswitz/PathShield/assets/10099969/4b8ab299-9a64-4466-80d5-d288dba59815)

## Overview
PathShield is an advanced anti-tracking tool designed for the M5StickC Plus, leveraging Bluetooth Low Energy (BLE) scanning to detect and identify potential tracking devices around you. With real-time device monitoring, manufacturer identification, and intuitive UI alerts, PathShield provides enhanced privacy and security on the go.

## Features
- Real-Time Scanning: Continuously scans for nearby BLE devices and monitors their signal strength.
- Manufacturer Identification: Uses a comprehensive MAC address prefix database to identify the manufacturer of detected devices.
- Device Tracking and History: Keeps track of detected devices, including signal stability and significant RSSI variations.
- User Interface: Interactive and responsive display with alert animations and device filtering options.
- Device Detection: Flags known trackers and important devices based on predefined MAC addresses.

## Installation
1. Clone the Repository:
```sh
git clone https://github.com/yourusername/PathShield.git
```

2. Add Libraries
- Ensure you have the required libraries:
  `M5StickCPlus, BLEDevice, BLEUtils, BLEScan`

3. Add Boards 
- Visit the board docs for [M5](https://docs.m5stack.com/en/arduino/arduino_board) for up to date instructions. 

4. Copy MacPrefixes.h & PathShield.ino files to your Arduino project directory

5. Select Boards > esp32 > M5StickC Plus
6. Click Upload Sketch

## Usage

- Power On: Turn on your M5StickC Plus.
- Start Scanning: The device will automatically start scanning for nearby BLE devices and display them on the screen.

  ![image](https://github.com/lukeswitz/PathShield/assets/10099969/c5572ec9-d8a4-419c-bee2-3ca9525348cf)


**Pause/Resume Scanning:**
- Press Button A to pause or resume scanning.
- When paused, the screen will display "Paused" with a red indicator.
- When resumed, the screen will display "Resumed" with a green indicator.

**Filter by Devices w/names:**
- Press Button B to toggle the name filter.
- When the filter is ON, the screen will display "Filter: ON" with a blue indicator.
- When the filter is OFF, the screen will display "Filter: OFF" with an orange indicator.

**Alerts:**
- If a new tracking device is detected, the screen will flash red and display "New Tracker Detected!" for 2 seconds, with the name MAC address of the device. 
- When it is a "special" tracker it will flash blue. Define these as you need.

**View Tracked Devices:**
- Detected devices and their information will be listed on the screen, including name, MAC address, manufacturer, and detection count.
- Devices flagged as trackers will be highlighted in red and kept at the top. 

**Monitor Screen:**
- The main screen shows the list of detected devices, updating in real-time as new devices are found and old devices are removed after a set detection window.
---

Disclaimer
> [!IMPORTANT]
>
> This repository and all associated code, documentation, and materials are provided "as-is" without any warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, and non-infringement. In no event shall the authors, contributors, or copyright holders be liable for any claim, damages, or other liability, whether in an action of contract, tort, or otherwise, arising from, out of, or in connection with the software or the use or other dealings in the software.
> By using this repository, you agree to take full responsibility for any results or consequences that may occur. The authors and contributors are not responsible for any misuse or damage caused by this software.


Credit: This project was inspired by and builds upon the work of the Creep Detector by @skickar.
