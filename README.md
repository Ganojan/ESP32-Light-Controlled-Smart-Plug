# ESP32-Light-Controlled-Smart-Plug
ESP32-based smart lighting automation using a VEML7700 lux sensor and Tuya cloud API. Monitors ambient light between 3-9PM, automatically turning on a smart plug when brightness drops below specific lux threshold.

---

# Description

I created this project to solve an issue I was having with my smart plug at home. To preface, my Tuya smart plug was used to control my living room light bulb. I used the built-in Globe smart plug app setting to turn on the plug and thus turn on my light bulb 40 mins before sunset (as that is usually the sweet spot for the light to turn it on). The problem was that on some days it would get dark earlier due to the day being gloomy and on other days it would still be bright out until sunset. Furthermore, the plug sometimes wouldn't even turn on due to the app failing to execute the command properly.

To combat this issue, I developed this light-controlled smart plug to ensure that the smart plug would turn on whenever the ambient light fell below a certain lux threshold.

---

# Features
- Ambient light sensing using VEML7700 sensor.
- Automatic smart plug control via Tuya Cloud API.
- 60 second buffer to ensure the ambient light is consitently below the threshold before turning plug on.
- Plug turns on instantly upon successfully undergoing the 60 second buffer.
- Time-restricted operation window (3PM to 9PM) to ensure plug doesn't turn on mid-day.
- Fully autonomous operation (user interaction not required after setup).
- ESP32 goes into deep sleep mode outside active hours to conserve energy.
- NTP-based real-time synchronization
- Secure request signing using HMAC-SHA256

---

# Components

**Hardware:**
- 1x ESP32-WROOM-32D
- 1x VEML7700 sensor
- 4x Male to Female Jumper Wires
- 1x Mini Breadboard
- 1x Micro-USB Cable

**Software:** Arduino IDE, Tuya Cloud API, Google/Pool NTP servers  
**Protocols:** HTTPS, I2C, WiFi, NTP

---

# File

- **ESP32_Light-Controlled_Smart_Plug.ino :** Main project file uploaded to the ESP32-WROOM-32D

---

# ESP32-WROOM-32D Pinout Diagram

<img style="display: block;-webkit-user-select: none;margin: auto;cursor: zoom-in;background-color: hsl(0, 0%, 90%);transition: background-color 300ms;" src="https://www.espboards.dev/img/JbAuVNG3Me-986.png" width="723" height="527">

---

## README by Ganojan Pathmasiri | May 4, 2026 ##
