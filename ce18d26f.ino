#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>  // Built-in web server instead of ESPAsyncWebServer
#include <HTTPClient.h>  // For cloud logging
#include <Adafruit_INA219.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <ESP32Servo.h>

// ===================== WiFi Configuration =====================
const char* WIFI_SSID = "pikachu";
const char* WIFI_PASSWORD = "asdfghjkl123";

// ===================== Firebase Configuration =====================
const char* FIREBASE_HOST = "https://battery-health-monitor-4ed0a-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* FIREBASE_AUTH = "AIzaSyBlJX0BWEyHvkwpSDeqQRe9o2L4TevkHPE";

// ===================== Web Server Setup =============================
WebServer server(80);  // Built-in web server
String deviceIP = "";

// ===================== OLED SPI Setup =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    4
#define OLED_DC       2
#define OLED_CS       5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RESET, OLED_CS);

// ===================== INA219 =====================
Adafruit_INA219 ina219;

// ===================== Thermistor Setup =====================
#define THERMISTOR_PIN 34
#define SERIES_RESISTOR 9400.0
#define BETA 3950.0
#define R0 10000.0f
#define T0 25.0 + 273.15

// ===================== Servo Setup =====================
#define SERVO_PIN 14
Servo myServo;
bool servoRunning = false;
int servoPos = 0;
int servoDirection = 1;
unsigned long servoLastUpdate = 0;
const int servoDelay = 15;

// ===================== Battery Setup =====================
#define BATTERY_MAH 2000.0
const float BATTERY_MAX_VOLTAGE = 4.2;
const float BATTERY_MIN_VOLTAGE = 3.0;
const int BATTERY_CELLS = 1;

float capacity_mAh = 0.0;
float initial_soc_percent = 100.0;
float soc_percent = 100.0;
bool soc_initialized = false;

// ===================== SOH (State of Health) Variables =====================
float soh_percent = 100.0;
float max_capacity_mAh = BATTERY_MAH;  // Rated capacity at new state
float avg_discharge_capacity_mAh = BATTERY_MAH;  // Average usable capacity
unsigned long cycle_count = 0;
unsigned long lastFullChargeTime = 0;
bool fullChargeDetected = false;

// ===================== Timing =====================
unsigned long previousMillis = 0;
unsigned long displayUpdateMillis = 0;
unsigned long cloudLogMillis = 0;
const unsigned long interval = 1000;
const unsigned long displayInterval = 3000;
const unsigned long cloudLogInterval = 60000;  // Log to cloud every minute

// ===================== Display States =====================
enum DisplayState {
  DISPLAY_BATTERY,
  DISPLAY_SOH,
  DISPLAY_NETWORK,
  DISPLAY_SERVO
};
DisplayState currentDisplayState = DISPLAY_BATTERY;

// ===================== Functions =====================
float calculateVoltageSoC(float cellVoltage) {
  if (cellVoltage >= BATTERY_MAX_VOLTAGE) {
    return 100.0;
  } else if (cellVoltage <= BATTERY_MIN_VOLTAGE) {
    return 0.0;
  } else {
    return ((cellVoltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
  }
}

// ===================== SOH Calculation Functions =====================
float calculateBatterySOH() {
  // Avoid division by zero
  if (max_capacity_mAh <= 0) return 100.0;
  
  // Calculate SOH based on average discharge capacity vs rated capacity
  float soh = (avg_discharge_capacity_mAh / max_capacity_mAh) * 100.0;
  soh = constrain(soh, 0.0, 100.0);
  return soh;
}

void updateBatterySOH() {
  // Detect full charge cycle (SoC >= 95%)
  if (soc_percent >= 95.0 && !fullChargeDetected) {
    fullChargeDetected = true;
    lastFullChargeTime = millis();
    Serial.println("Full charge detected - SOH cycle tracking started");
  }
  
  // Detect discharge cycle completion (SoC <= 20% after full charge)
  if (fullChargeDetected && soc_percent <= 20.0) {
    // Calculate actual usable capacity for this cycle
    float cycle_capacity = capacity_mAh;
    
    // Update average discharge capacity using exponential moving average
    if (cycle_count == 0) {
      avg_discharge_capacity_mAh = cycle_capacity;
    } else {
      // Weighted average: 80% previous + 20% current cycle
      avg_discharge_capacity_mAh = (avg_discharge_capacity_mAh * 0.8) + (cycle_capacity * 0.2);
    }
    
    // Update SOH
    soh_percent = calculateBatterySOH();
    
    // Increment cycle count
    cycle_count++;
    fullChargeDetected = false;
    
    Serial.printf("SOH Update - Cycle: %lu, Capacity: %.2f mAh, SOH: %.1f%%\n", 
                  cycle_count, avg_discharge_capacity_mAh, soh_percent);
  }
  
  // Alternative SOH calculation based on capacity degradation over time
  if (capacity_mAh > 0) {
    float instantSOH = ((BATTERY_MAH - capacity_mAh) / BATTERY_MAH) * 100.0;
    instantSOH = constrain(instantSOH, 0.0, 100.0);
    
    // Use minimum of cycle-based and instant SOH for conservative estimate
    soh_percent = min(soh_percent, instantSOH);
  }
}

// ===================== Cloud Logging Function =====================
void logToFirebase(float packVoltage, float cellVoltage, float current_mA, float soc_percent, float soh_percent, float capacity_mAh, float tempC) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String timestamp = String(millis());
    String path = String(FIREBASE_HOST) + "/battery_data/" + timestamp + ".json?auth=" + FIREBASE_AUTH;
    
    String json = "{";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"packVoltage\":" + String(packVoltage, 3) + ",";
    json += "\"cellVoltage\":" + String(cellVoltage, 3) + ",";
    json += "\"current_mA\":" + String(current_mA, 2) + ",";
    json += "\"soc_percent\":" + String(soc_percent, 2) + ",";
    json += "\"soh_percent\":" + String(soh_percent, 2) + ",";
    json += "\"capacity_mAh\":" + String(capacity_mAh, 3) + ",";
    json += "\"tempC\":" + String(tempC, 1) + ",";
    json += "\"cycle_count\":" + String(cycle_count) + ",";
    json += "\"servoRunning\":" + String(servoRunning ? "true" : "false");
    json += "}";

    http.begin(path.c_str());
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.PUT(json);
    http.end();

    if (httpResponseCode > 0) {
      Serial.printf("Firebase upload successful: %d\n", httpResponseCode);
    } else {
      Serial.printf("Firebase upload failed: %d\n", httpResponseCode);
    }
  }
}

void updateOLEDDisplay(float packVoltage, float cellVoltage, float current_mA, float tempC) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  switch(currentDisplayState) {
    case DISPLAY_BATTERY:
      display.println("=== BATTERY INFO ===");
      display.print("Pack: "); display.print(packVoltage, 2); display.println("V");
      display.print("Cell: "); display.print(cellVoltage, 2); display.println("V");
      display.print("Current: "); display.print(current_mA, 1); display.println("mA");
      display.print("SoC: "); display.print(soc_percent, 1); display.println("%");
      display.print("Used: "); display.print(capacity_mAh, 1); display.println("mAh");
      break;

    case DISPLAY_SOH:
      display.println("=== HEALTH INFO ===");
      display.print("SoH: "); display.print(soh_percent, 1); display.println("%");
      display.print("Max Cap: "); display.print(max_capacity_mAh, 0); display.println("mAh");
      display.print("Avg Cap: "); display.print(avg_discharge_capacity_mAh, 0); display.println("mAh");
      display.print("Cycles: "); display.println(cycle_count);
      display.print("Temp: "); display.print(tempC, 1); display.println("C");
      break;

    case DISPLAY_NETWORK:
      display.println("=== NETWORK INFO ===");
      display.print("SSID: "); display.println(WIFI_SSID);
      display.print("IP: "); display.println(deviceIP);
      display.print("Status: "); 
      display.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      display.print("Web: http://");
      display.println(deviceIP);
      break;

    case DISPLAY_SERVO:
      display.println("=== SERVO STATUS ===");
      display.print("State: ");
      display.println(servoRunning ? "RUNNING" : "STOPPED");
      display.print("Position: "); display.print(servoPos); display.println("°");
      display.print("Direction: ");
      display.println(servoDirection == 1 ? "Forward" : "Reverse");
      display.print("Temp: "); display.print(tempC, 1); display.println("°C");
      break;
  }

  display.display();
}

void initializeSoC() {
  Serial.println("Initializing SoC from voltage reading...");
  delay(500);

  float shuntVoltage = ina219.getShuntVoltage_mV();
  float busVoltage = ina219.getBusVoltage_V();
  float loadVoltage = busVoltage + (shuntVoltage / 1000.0);
  float cellVoltage = loadVoltage / BATTERY_CELLS;

  initial_soc_percent = calculateVoltageSoC(cellVoltage);
  soc_percent = initial_soc_percent;
  soc_initialized = true;
  capacity_mAh = 0.0;

  Serial.printf("Initial Pack Voltage: %.2f V\n", loadVoltage);
  Serial.printf("Initial Cell Voltage: %.2f V\n", cellVoltage);
  Serial.printf("Initial SoC: %.1f %%\n", initial_soc_percent);
  Serial.printf("Initial SoH: %.1f %%\n", soh_percent);
  Serial.println("Battery monitor ready!");
}

// ===================== Web Server HTML (Stored in PROGMEM) =====================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Battery Monitor & Control</title>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { 
      font-family: 'Segoe UI', Arial, sans-serif; 
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      min-height: 100vh;
      padding: 10px;
    }
    .container { 
      max-width: 1200px; 
      margin: 0 auto; 
      background: rgba(255,255,255,0.1);
      padding: 20px;
      border-radius: 20px;
      backdrop-filter: blur(15px);
      box-shadow: 0 8px 32px rgba(0,0,0,0.3);
    }
    .header { 
      text-align: center; 
      margin-bottom: 30px;
      padding-bottom: 20px;
      border-bottom: 2px solid rgba(255,255,255,0.2);
    }
    .header h1 { 
      font-size: 2.5em; 
      margin-bottom: 10px;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
    }
    .status-indicator { 
      display: inline-block;
      width: 12px;
      height: 12px;
      border-radius: 50%;
      margin-right: 8px;
    }
    .status-online { background-color: #00ff88; }
    .status-offline { background-color: #ff4757; }
    
    .data-grid { 
      display: grid; 
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); 
      gap: 20px; 
      margin-bottom: 30px;
    }
    .data-card { 
      background: rgba(255,255,255,0.15); 
      padding: 20px; 
      border-radius: 15px;
      text-align: center;
      transition: transform 0.3s ease;
      border: 1px solid rgba(255,255,255,0.2);
    }
    .data-card:hover { 
      transform: translateY(-5px);
      background: rgba(255,255,255,0.2);
    }
    .data-value { 
      font-size: 2.2em; 
      font-weight: bold; 
      margin: 15px 0;
      color: #FFD700;
      text-shadow: 1px 1px 2px rgba(0,0,0,0.5);
    }
    .data-label { 
      font-size: 0.9em; 
      opacity: 0.9;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .data-unit {
      font-size: 0.7em;
      opacity: 0.8;
      margin-left: 5px;
    }
    
    .progress-container {
      margin: 15px 0;
    }
    .progress-bar {
      width: 100%;
      height: 25px;
      background: rgba(255,255,255,0.2);
      border-radius: 12px;
      overflow: hidden;
      position: relative;
    }
    .progress-fill {
      height: 100%;
      background: linear-gradient(90deg, #ff7675, #fdcb6e, #00b894);
      transition: width 0.5s ease;
      border-radius: 12px;
    }
    .progress-text {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      font-weight: bold;
      text-shadow: 1px 1px 2px rgba(0,0,0,0.7);
      font-size: 0.9em;
    }
    
    .soh-progress-fill {
      background: linear-gradient(90deg, #e74c3c, #f39c12, #27ae60);
    }
    
    .controls { 
      background: rgba(255,255,255,0.1);
      padding: 25px;
      border-radius: 15px;
      text-align: center; 
      margin: 20px 0;
    }
    .controls h3 {
      margin-bottom: 20px;
      font-size: 1.4em;
    }
    .button-group {
      display: flex;
      gap: 15px;
      justify-content: center;
      flex-wrap: wrap;
    }
    button { 
      background: linear-gradient(45deg, #ff6b6b, #ee5a24);
      color: white; 
      border: none; 
      padding: 15px 25px; 
      border-radius: 25px;
      cursor: pointer; 
      font-size: 1em;
      font-weight: bold;
      transition: all 0.3s ease;
      box-shadow: 0 4px 15px rgba(0,0,0,0.2);
      min-width: 120px;
    }
    button:hover { 
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(0,0,0,0.3);
    }
    button:active {
      transform: translateY(0);
    }
    button.stop-btn { 
      background: linear-gradient(45deg, #2d3436, #636e72);
    }
    button.reset-btn { 
      background: linear-gradient(45deg, #0984e3, #74b9ff);
    }
    button:disabled {
      opacity: 0.6;
      cursor: not-allowed;
      transform: none;
    }
    
    .servo-status { 
      background: rgba(255,255,255,0.1);
      padding: 20px;
      border-radius: 15px;
      margin: 20px 0;
    }
    .servo-status h3 {
      text-align: center;
      margin-bottom: 15px;
    }
    .status-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 15px;
    }
    .status-item {
      text-align: center;
      padding: 10px;
      background: rgba(255,255,255,0.1);
      border-radius: 10px;
    }
    
    .last-update {
      text-align: center;
      margin-top: 20px;
      font-size: 0.9em;
      opacity: 0.8;
    }
    
    @media (max-width: 768px) {
      .data-grid { 
        grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); 
      }
      .button-group {
        flex-direction: column;
        align-items: center;
      }
      button {
        width: 100%;
        max-width: 200px;
      }
    }
  </style>
  <script>
    let servoStatus = false;
    let lastUpdateTime = new Date();

    function fetchData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          // Update battery data
          document.getElementById('packVoltage').textContent = data.packVoltage.toFixed(2);
          document.getElementById('cellVoltage').textContent = data.cellVoltage.toFixed(2);
          document.getElementById('current').textContent = data.current_mA.toFixed(1);
          document.getElementById('soc').textContent = data.soc.toFixed(1);
          document.getElementById('soh').textContent = data.soh.toFixed(1);
          document.getElementById('capacity').textContent = data.capacity_mAh.toFixed(3);
          document.getElementById('temp').textContent = data.tempC.toFixed(1);
          document.getElementById('cycleCount').textContent = data.cycle_count;
          
          // Update servo status
          document.getElementById('servoStatusText').textContent = data.servoRunning ? 'RUNNING' : 'STOPPED';
          document.getElementById('servoPosition').textContent = data.servoPosition + '°';
          document.getElementById('servoDirection').textContent = 
            data.servoDirection === 1 ? 'Forward' : 'Reverse';

          // Update progress bars
          const socProgressBar = document.getElementById('socProgress');
          const socProgressText = document.getElementById('socProgressText');
          socProgressBar.style.width = Math.max(0, Math.min(100, data.soc)) + '%';
          socProgressText.textContent = data.soc.toFixed(1) + '%';

          const sohProgressBar = document.getElementById('sohProgress');
          const sohProgressText = document.getElementById('sohProgressText');
          sohProgressBar.style.width = Math.max(0, Math.min(100, data.soh)) + '%';
          sohProgressText.textContent = data.soh.toFixed(1) + '%';

          // Update connection status
          const statusIndicator = document.querySelector('.status-indicator');
          statusIndicator.className = 'status-indicator status-online';

          // Update servo status
          servoStatus = data.servoRunning;
          updateButtons();
          
          // Update last update time
          lastUpdateTime = new Date();
          document.getElementById('lastUpdate').textContent = 
            'Last updated: ' + lastUpdateTime.toLocaleTimeString();
        })
        .catch(error => {
          console.error('Fetch error:', error);
          const statusIndicator = document.querySelector('.status-indicator');
          statusIndicator.className = 'status-indicator status-offline';
        });
    }

    function sendCommand(cmd) {
      fetch('/cmd?c=' + cmd)
        .then(response => response.text())
        .then(result => {
          console.log('Command result:', result);
          if(cmd === 'O') servoStatus = true;
          if(cmd === 'S') servoStatus = false;
          updateButtons();
          
          // Show feedback
          showNotification('Command sent: ' + result);
        })
        .catch(error => {
          console.error('Command error:', error);
          showNotification('Error sending command', true);
        });
    }

    function updateButtons() {
      const startBtn = document.getElementById('startBtn');
      const stopBtn = document.getElementById('stopBtn');
      startBtn.disabled = servoStatus;
      stopBtn.disabled = !servoStatus;
    }

    function showNotification(message, isError = false) {
      const notification = document.createElement('div');
      notification.textContent = message;
      notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        padding: 15px 25px;
        background: ${isError ? '#ff4757' : '#2ed573'};
        color: white;
        border-radius: 10px;
        z-index: 1000;
        font-weight: bold;
        box-shadow: 0 4px 15px rgba(0,0,0,0.2);
      `;
      document.body.appendChild(notification);
      setTimeout(() => {
        document.body.removeChild(notification);
      }, 3000);
    }

    // Initialize
    document.addEventListener('DOMContentLoaded', function() {
      fetchData();
      setInterval(fetchData, 1000); // Update every second
      updateButtons();
    });
  </script>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>🔋 ESP32 Battery Monitor</h1>
      <p><span class="status-indicator status-offline"></span>Real-time Battery & Servo Control with SOH Tracking</p>
    </div>

    <div class="data-grid">
      <div class="data-card">
        <div class="data-label">Pack Voltage</div>
        <div class="data-value">
          <span id="packVoltage">--</span>
          <span class="data-unit">V</span>
        </div>
      </div>
      
      <div class="data-card">
        <div class="data-label">Cell Voltage</div>
        <div class="data-value">
          <span id="cellVoltage">--</span>
          <span class="data-unit">V</span>
        </div>
      </div>
      
      <div class="data-card">
        <div class="data-label">Current</div>
        <div class="data-value">
          <span id="current">--</span>
          <span class="data-unit">mA</span>
        </div>
      </div>
      
      <div class="data-card">
        <div class="data-label">State of Charge</div>
        <div class="data-value">
          <span id="soc">--</span>
          <span class="data-unit">%</span>
        </div>
        <div class="progress-container">
          <div class="progress-bar">
            <div class="progress-fill" id="socProgress"></div>
            <div class="progress-text" id="socProgressText">--%</div>
          </div>
        </div>
      </div>

      <div class="data-card">
        <div class="data-label">State of Health</div>
        <div class="data-value">
          <span id="soh">--</span>
          <span class="data-unit">%</span>
        </div>
        <div class="progress-container">
          <div class="progress-bar">
            <div class="progress-fill soh-progress-fill" id="sohProgress"></div>
            <div class="progress-text" id="sohProgressText">--%</div>
          </div>
        </div>
      </div>
      
      <div class="data-card">
        <div class="data-label">Capacity Used</div>
        <div class="data-value">
          <span id="capacity">--</span>
          <span class="data-unit">mAh</span>
        </div>
      </div>
      
      <div class="data-card">
        <div class="data-label">Temperature</div>
        <div class="data-value">
          <span id="temp">--</span>
          <span class="data-unit">°C</span>
        </div>
      </div>

      <div class="data-card">
        <div class="data-label">Charge Cycles</div>
        <div class="data-value">
          <span id="cycleCount">--</span>
          <span class="data-unit">cycles</span>
        </div>
      </div>
    </div>

    <div class="servo-status">
      <h3>🎛️ Servo Motor Status</h3>
      <div class="status-grid">
        <div class="status-item">
          <strong>Status:</strong><br>
          <span id="servoStatusText">--</span>
        </div>
        <div class="status-item">
          <strong>Position:</strong><br>
          <span id="servoPosition">--</span>
        </div>
        <div class="status-item">
          <strong>Direction:</strong><br>
          <span id="servoDirection">--</span>
        </div>
      </div>
    </div>

    <div class="controls">
      <h3>🎮 Remote Control</h3>
      <div class="button-group">
        <button id="startBtn" onclick="sendCommand('O')">
          🔄 Start Servo
        </button>
        <button id="stopBtn" class="stop-btn" onclick="sendCommand('S')">
          ⏹️ Stop Servo
        </button>
        <button class="reset-btn" onclick="sendCommand('R')">
          🔄 Reset SoC
        </button>
      </div>
    </div>

    <div class="last-update">
      <span id="lastUpdate">Connecting...</span>
    </div>
  </div>
</body>
</html>
)rawliteral";

// ===================== Web Server Handler Functions =====================
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleData() {
  // Read current sensor values
  float shuntV = ina219.getShuntVoltage_mV();
  float busV = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  float packV = busV + (shuntV / 1000.0);
  float cellV = packV / BATTERY_CELLS;

  // Read temperature
  int adcReading = analogRead(THERMISTOR_PIN);
  float voltage = adcReading * 3.3 / 4095.0;
  voltage = constrain(voltage, 0.01, 3.29);
  float resistance = SERIES_RESISTOR * (3.3 / voltage - 1.0);
  resistance = max(resistance, (float)R0);
  float tempK = 1.0 / (1.0 / T0 + (1.0 / BETA) * log(resistance / R0));
  float tempC = tempK - 273.15;
  if (tempC < -50 || tempC > 100) tempC = 25.0;

  // Create JSON response
  String json = "{";
  json += "\"packVoltage\":" + String(packV, 3) + ",";
  json += "\"cellVoltage\":" + String(cellV, 3) + ",";
  json += "\"current_mA\":" + String(current_mA, 2) + ",";
  json += "\"soc\":" + String(soc_percent, 2) + ",";
  json += "\"soh\":" + String(soh_percent, 2) + ",";
  json += "\"capacity_mAh\":" + String(capacity_mAh, 4) + ",";
  json += "\"tempC\":" + String(tempC, 1) + ",";
  json += "\"cycle_count\":" + String(cycle_count) + ",";
  json += "\"servoRunning\":" + String(servoRunning ? "true" : "false") + ",";
  json += "\"servoPosition\":" + String(servoPos) + ",";
  json += "\"servoDirection\":" + String(servoDirection);
  json += "}";

  server.send(200, "application/json", json);
}

void handleCommand() {
  String response = "OK";
  
  if (server.hasArg("c")) {
    char cmd = server.arg("c")[0];
    
    switch(cmd) {
      case 'O':
      case 'o':
        servoRunning = true;
        Serial.println("Web: Servo started");
        response = "Servo Started";
        break;
        
      case 'S':
      case 's':
        servoRunning = false;
        myServo.write(0);
        servoPos = 0;
        servoDirection = 1;
        Serial.println("Web: Servo stopped");
        response = "Servo Stopped";
        break;
        
      case 'R':
      case 'r':
        initializeSoC();
        Serial.println("Web: SoC reset");
        response = "SoC Reset";
        break;
        
      default:
        response = "Unknown command";
        Serial.println("Web: Unknown command received");
    }
  } else {
    response = "No command specified";
  }
  
  server.send(200, "text/plain", response);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

void setupWebServer() {
  // Setup web server handlers
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/cmd", handleCommand);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web server started successfully");
  Serial.println("Access your battery monitor at: http://" + deviceIP);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 Battery Monitor with SOH Tracking Starting...");

  Serial.printf("ESP32 voltage: %.2fV\n", analogRead(A0) * 3.3 / 4095.0 * 2);
  delay(2000); // Power stabilization delay

  // Initialize INA219
  if (!ina219.begin()) {
    Serial.println("ERROR: Failed to find INA219 chip!");
    while (1) {
      delay(1000);
      Serial.println("Please check INA219 wiring...");
    }
  }
  ina219.setCalibration_16V_400mA();
  Serial.println("✓ INA219 initialized successfully");

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x00)) {
    Serial.println("ERROR: SSD1306 OLED allocation failed!");
    while (1) {
      delay(1000);
      Serial.println("Please check OLED SPI wiring...");
    }
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Battery Monitor");
  display.println("with SOH Tracking");
  display.println("Starting WiFi...");
  display.display();
  Serial.println("✓ OLED initialized successfully");

  // Initialize Servo
  myServo.attach(SERVO_PIN);
  myServo.write(0);
  delay(500);
  Serial.println("✓ Servo initialized successfully");

  // Initialize SoC and SOH
  initializeSoC();

  // Connect to WiFi
  Serial.println("Connecting to WiFi: " + String(WIFI_SSID));
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting to WiFi...");
  display.println(WIFI_SSID);
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int wifiAttempts = 0;
  Serial.print("WiFi connection progress: ");
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
    delay(100);
    yield(); // Allow other tasks to run
    Serial.print(".");
    wifiAttempts++;
    
    if (wifiAttempts % 5 == 0) {
      Serial.println(); 
      Serial.printf("Attempt %d/30...", wifiAttempts);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    deviceIP = WiFi.localIP().toString();
    Serial.println();
    Serial.println("✓ WiFi connected successfully!");
    Serial.println("Device IP address: " + deviceIP);
    Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");

    // Show connection info on OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Connected!");
    display.println("SSID: " + String(WIFI_SSID));
    display.println("IP: " + deviceIP);
    display.println("");
    display.println("Web Interface:");
    display.println("http://" + deviceIP);
    display.display();
    delay(3000);

    // Setup web server
    setupWebServer();
  } else {
    Serial.println();
    Serial.println("✗ WiFi connection failed!");
    Serial.println("Running in offline mode...");
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Failed!");
    display.println("Check credentials");
    display.println("Running offline...");
    display.display();
    delay(2000);
  }

  Serial.println("=================================");
  Serial.println("Setup completed successfully!");
  Serial.println("Available commands:");
  Serial.println("  O - Start servo motor");
  Serial.println("  S - Stop servo motor"); 
  Serial.println("  R - Reset SoC counter");
  Serial.println("  H - Show help");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Web interface: http://" + deviceIP);
    Serial.println("Cloud logging enabled to Firebase");
  }
  Serial.println("=================================");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Handle web server requests (IMPORTANT: This must be called regularly)
  server.handleClient();
  
  // Feed watchdog timer
  yield(); 

  // ================== Battery Monitoring (Non-blocking) ==================
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Read INA219 sensors
    float shuntVoltage = ina219.getShuntVoltage_mV();
    float busVoltage = ina219.getBusVoltage_V();
    float current_mA = ina219.getCurrent_mA();
    float loadVoltage = busVoltage + (shuntVoltage / 1000.0);
    float cellVoltage = loadVoltage / BATTERY_CELLS;
    float soc_voltage = calculateVoltageSoC(cellVoltage);

    // Coulomb counting (only if discharging)
    if (current_mA > 0) {
      capacity_mAh += current_mA * (interval / 3600000.0);
    }

    // Calculate SoC based on initial voltage reading minus usage
    float capacity_used_percent = (capacity_mAh / BATTERY_MAH) * 100.0;
    soc_percent = initial_soc_percent - capacity_used_percent;
    soc_percent = constrain(soc_percent, 0.0, 100.0);

    // Update SOH calculation
    updateBatterySOH();

    // Read thermistor
    int adcReading = analogRead(THERMISTOR_PIN);
    float voltage = adcReading * 3.3 / 4095.0;
    voltage = constrain(voltage, 0.01, 3.29);
    float resistance = SERIES_RESISTOR * (3.3 / voltage - 1.0);
    resistance = max(resistance, R0);
    float tempK = 1.0 / (1.0 / T0 + (1.0 / BETA) * log(resistance / R0));
    float tempC = tempK - 273.15;
    if (tempC < -50 || tempC > 100) tempC = 25.0;

    // Update OLED display with rotation (now includes SOH screen)
    if (currentMillis - displayUpdateMillis >= displayInterval) {
      displayUpdateMillis = currentMillis;
      currentDisplayState = (DisplayState)((currentDisplayState + 1) % 4);  // Changed to 4 states
    }
    updateOLEDDisplay(loadVoltage, cellVoltage, current_mA, tempC);

    // Serial output for debugging
    Serial.println("=== Battery Monitor Data ===");
    Serial.printf("Pack Voltage: %.2f V\n", loadVoltage);
    Serial.printf("Cell Voltage: %.2f V\n", cellVoltage);
    Serial.printf("Current: %.1f mA\n", current_mA);
    Serial.printf("Initial SoC: %.1f %%\n", initial_soc_percent);
    Serial.printf("Current SoC: %.1f %%\n", soc_percent);
    Serial.printf("State of Health: %.1f %%\n", soh_percent);
    Serial.printf("Capacity Used: %.4f mAh\n", capacity_mAh);
    Serial.printf("Average Capacity: %.2f mAh\n", avg_discharge_capacity_mAh);
    Serial.printf("Cycle Count: %lu\n", cycle_count);
    Serial.printf("Temperature: %.1f °C\n", tempC);
    Serial.printf("Servo Status: %s (Pos: %d°)\n", servoRunning ? "RUNNING" : "STOPPED", servoPos);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi: Connected (RSSI: %d dBm)\n", WiFi.RSSI());
    } else {
      Serial.println("WiFi: Disconnected");
    }
    Serial.println("============================");

    // Cloud logging
    if (WiFi.status() == WL_CONNECTED && (currentMillis - cloudLogMillis >= cloudLogInterval)) {
      cloudLogMillis = currentMillis;
      logToFirebase(loadVoltage, cellVoltage, current_mA, soc_percent, soh_percent, capacity_mAh, tempC);
    }

    // Monitor memory usage
    static unsigned long lastMemCheck = 0;
    if (currentMillis - lastMemCheck > 10000) { // Check every 10 seconds
      Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
      Serial.printf("Largest free block: %d bytes\n", ESP.getMaxAllocHeap());
      lastMemCheck = currentMillis;
    }
  }

  // ================== Serial Command Processing ==================
  if (Serial.available()) {
    char cmd = Serial.read();
    switch(cmd) {
      case 'O':
      case 'o':
        servoRunning = true;
        Serial.println("Serial: Servo started");
        break;
        
      case 'S':
      case 's':
        servoRunning = false;
        myServo.write(0);
        servoPos = 0;
        servoDirection = 1;
        Serial.println("Serial: Servo stopped");
        break;
        
      case 'R':
      case 'r':
        initializeSoC();
        Serial.println("Serial: SoC reset");
        break;
        
      case 'H':
      case 'h':
        Serial.println("Available commands:");
        Serial.println("  O - Start servo motor");
        Serial.println("  S - Stop servo motor");
        Serial.println("  R - Reset SoC counter");
        Serial.println("  H - Show this help");
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("Web interface: http://" + deviceIP);
        }
        break;
        
      default:
        Serial.println("Unknown command. Type 'H' for help.");
    }
  }

  // ================== Non-blocking Servo Control ==================
  if (servoRunning && (currentMillis - servoLastUpdate >= servoDelay)) {
    servoLastUpdate = currentMillis;
    
    // Check power supply before moving servo (safety feature)
    float busVoltage = ina219.getBusVoltage_V();
    if (busVoltage < 4.0) { // Minimum voltage check
      Serial.println("Warning: Low voltage detected, stopping servo for safety");
      servoRunning = false;
      return;
    }

    myServo.write(servoPos);
    servoPos += servoDirection;

    // Change direction at limits
    if (servoPos >= 180) {
      servoDirection = -1;
    } else if (servoPos <= 0) {
      servoDirection = 1;
    }
  }

  // WiFi connection monitoring and auto-reconnect
  static unsigned long lastWiFiCheck = 0;
  if (currentMillis - lastWiFiCheck > 30000) { // Check every 30 seconds
    lastWiFiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected. Attempting to reconnect...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // Small delay to prevent excessive CPU usage
  delay(1);
}