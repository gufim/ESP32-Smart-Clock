🚀 Changelog
Version V2.0

    Improved Sensor Connectivity: Refactored the core communication logic between the external sensor and the clock for rock-solid stability.

    Visual & UI Fixes: Enhanced the display layouts, fixed rendering bugs, and polished individual module icons.

    Local WiFi & STA Mode Support: Added the ability to connect the clock to your home WiFi network. The system now operates efficiently in dual AP + STA mode.

    Interactive Status & IP Scroller: Pressing both hardware buttons simultaneously now triggers a smooth scrolling text displaying the current network mode and the configuration page IP address.

    NTP Time Synchronization: Introduced automatic time synchronization via NTP servers to keep the RTC highly accurate.

    Time Zone Adjustment: Added a web interface feature to easily correct and shift your local UTC time zone.


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
