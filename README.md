## What's New in Version 2.0

This major update focuses on connection stability, user experience, and automated time management.

### Key Improvements & Features

*   **Rock-Solid Connectivity**
    *   Complete refactoring of the communication layer between the external sensor and the clock.
    *   Eliminated packet drops and improved ESP-NOW stability.
*   **Visual & Graphics Polishing**
    *   Enhanced layout alignment for individual modules.
    *   Fixed rendering glitches and polished custom font icons (battery, RSSI, and weather symbols).
*   **Local WiFi & Dual-Mode (STA + AP)**
    *   Added full support for connecting the clock to your home WiFi router.
    *   The system now operates seamlessly in AP + STA mode, allowing local web config and background internet data access at the same time.

### New Interactive Controls & Web Features

*   **Dual-Button Shortcut:** Pressing both hardware buttons simultaneously triggers a smooth scrolling text on the LED matrix, showing the active network mode and the current Web Server IP address.
*   **NTP Time Sync:** Automated real-time synchronization with global NTP servers (pool.ntp.org / time.google.com) to prevent RTC time drifting.
*   **Time Zone Settings:** Easily adjust and save your UTC time zone directly through the web interface to automatically handle regional time shifts.

# ESP32-C3 Smart Clock & ESP32-S3 Outdoor Sensor

A high-efficiency, advanced LED Matrix clock project featuring a wireless outdoor temperature station. 

## 🛠 Developed On
* **OS:** Linux Mint (KDE Plasma)
* **IDE:** Arduino IDE 2.3.8
* **Hardware:** * Main Clock: **ESP32-C3 Super Mini**
  * Outdoor Sensor: **ESP32-S3**

## 🌟 Key Features
* **Custom Display Engine:** Manual character drawing on 4x1 MAX7219 Matrix (Zero-flicker).
* **Sensors:** Local Pressure (BMP280), Humidity (AHTX0), and RTC (DS1307).
* **Wireless Sensor:** ESP32-S3 using ESP-NOW (Battery optimized, >2 weeks life).
* **Web UI:** Configuration panel at `192.168.5.1` (Isolated subnet to avoid conflicts).
* **Smart Night Mode:** Automatic LDR dimming and dynamic reporting intervals for energy saving.

## 🔌 Connection Diagram (Main Clock - ESP32-C3)

| Component | ESP32-C3 Pin | Notes |
| :--- | :--- | :--- |
| **MAX7219 DIN** | GPIO 6 | Data |
| **MAX7219 CS** | GPIO 7 | Chip Select |
| **MAX7219 CLK** | GPIO 3 | Clock |
| **RTC / BMP / AHT SDA** | GPIO 8 | I2C Data |
| **RTC / BMP / AHT SCL** | GPIO 9 | I2C Clock |
| **LDR (Photoresistor)** | GPIO 0 | Analog Brightness Sensor |
| **Hour Button** | GPIO 1 | Pull-up |
| **Minute Button** | GPIO 2 | Pull-up |

---
*Developed with passion on Linux Mint KDE.*

## Video Demo
[![Watch the video](https://img.youtube.com/vi/ID_FILMU/0.jpg)](https://www.youtube.com/watch?v=ttarTMbDBVQ)
