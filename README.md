IoT-Based Cloud-Integrated EV Battery Health Monitoring System


Real-time battery monitoring system using ESP32, INA226, NTC Thermistor, Firebase, and a Web Dashboard to track EV battery voltage, current, temperature, SoC, SoH, and cycle count.
The system also provides remote servo control, cloud logging, and predictive maintenance insights.

🚀 Project Overview

Electric vehicle (EV) batteries degrade silently due to irregular charging, harsh temperatures, and excessive current loads. This project solves that problem by building a smart IoT system that continuously measures battery health and sends data to the cloud.

The system:

Monitors Voltage, Current, Temperature, SoC, SoH
Logs data to Firebase Realtime Database
Displays real-time metrics on a web dashboard
Provides a mobile-friendly UI
Supports remote control of a servo motor
Performs State of Health (SoH) tracking
Sends alerts when thresholds are crossed

🧩 Features

✔ Real-Time Monitoring
Battery pack voltage
Individual cell voltage
Current (mA)
Temperature (°C)
State of Charge (SoC)
State of Health (SoH)
Capacity used (mAh)
Charge cycles

✔ Cloud Features

Firebase integration
JSON data logging every 60 seconds
Historical data visualization
Multi-device accessibility

✔ Web Dashboard

Fully responsive interface
Live updates every 1 second
SoC and SoH progress bars
Last updated timestamp
Servo control buttons
Connection status indicator

✔ Hardware Features

ESP32 edge computing
INA226 current + voltage sensor
NTC thermistor for temperature
OLED display for local monitoring
Servo motor integration
Safety checks (voltage limits, watchdog)

🛠 Tech Stack
Hardware

ESP32
INA226 Current/Power Sensor
NTC Thermistor
18650 Li-ion battery pack (3S/4S)
LM2596 buck converter
OLED Display (SSD1306)
Servo Motor

Software

Arduino IDE
Firebase Realtime Database
HTML / CSS / JS Web Interface
C++ (Arduino Core)



▶️ How to Run
1. Install Libraries
Adafruit INA219
Adafruit GFX
Adafruit SSD1306
ESP32Servo
HTTPClient
2. Update WiFi & Firebase credentials
3. Upload Code to ESP32
Using Arduino IDE.
4. Open Serial Monitor
It shows:
Pack voltage
SoC
SoH
WiFi IP
Firebase logs
Visit the IP address for the webpage 
