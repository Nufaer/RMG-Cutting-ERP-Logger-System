#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h> // The only graphics library we need now
#include <RTClib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EspUsbHost.h> 

// --- Hardware Pin Definitions (ESP32-S3) ---
#define I2C_SDA 8
#define I2C_SCL 18
#define BUZZER_PIN 4

// --- WiFi & Backend Configurations ---
const char* ssid = "TOP-MGMT-MKDL";
const char* password = "Metro@2024";
const char* erpNextURL = "https://perp.panamgroupbd.com/api/resource/RMG Cutting Scan";
const char* erpApiKey = "fb20ef931cd1d1f";
const char* erpApiSecret = "0dc1b58906d49b9";

struct WebLogPayload {
  char scannedCode[64];
  char scanTime[32];
  char unit[32];
  char tableNo[32];
  int stage;
  char stageName[32];
  int stageCount;
  int totalCount;
};

QueueHandle_t webLogQueue = NULL;

// --- Global Objects ---
TFT_eSPI tft = TFT_eSPI();
RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 21600); 
EspUsbHost usbHost;

// --- Process State Machine ---
enum ProcessStage {
  STAGE_IDLE,
  STAGE_BUNDLING,
  STAGE_Q_CHECK,
  STAGE_REPLACE,
  STAGE_S_INPUT
};

// --- UI Screen States ---
enum ScreenState {
  SCREEN_EMPTY,
  SCREEN_WORKING,
  SCREEN_WORK_SCAN,
  SCREEN_CONFIRM_STOP
};

ProcessStage currentStage = STAGE_IDLE;
ScreenState currentScreen = SCREEN_EMPTY;

String currentUnit = "Unit 1";   
String currentTable = "Table A"; 
String lastScannedCode = "None";
String lastTimeStr = "";

int totalCount = 0;
int stageCount = 0;
int rejectCount = 0;
int replaceCount = 0;

unsigned long lastScanTime = 0;
unsigned long lastRTCUpdate = 0;
const unsigned long INACTIVITY_TIMEOUT = 5000; // 5 seconds to return to working screen after scan

String qrBuffer = ""; 
String finalScanData = "";
volatile bool newScanReady = false;

// Button zones (X, Y, Width, Height)
const int btnY = 190; 
const int btnH = 40;
// Working screen buttons
const int btnBackX = 10, btnBackW = 90;
const int btnStopX = 115, btnStopW = 90;
const int btnNextX = 220, btnNextW = 90;
// Start screen button
const int btnStartX = 110, btnStartW = 100;
// Confirm screen buttons
const int btnYesX = 40, btnYesW = 100;
const int btnNoX = 180, btnNoW = 100;

unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 300;

void drawScreen();
void updateHeaderTime();
void updateDynamicData();
void handleScanData(String scanData);
void postDataToERPNext(String scanData);
void drawButton(int x, int y, int w, int h, const char* label, uint16_t color);
const char* getStageName();
void triggerBuzzer();

void webLogTask(void *pvParameters) {
  WebLogPayload payload;
  while (1) {
    if (xQueueReceive(webLogQueue, &payload, portMAX_DELAY) == pdPASS) {
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(erpNextURL);
        http.addHeader("Content-Type", "application/json");
        String auth = "token " + String(erpApiKey) + ":" + String(erpApiSecret);
        http.addHeader("Authorization", auth);

        String json = "{";
        json += "\"scanned_code\": \"" + String(payload.scannedCode) + "\",";
        json += "\"scan_time\": \"" + String(payload.scanTime) + "\",";
        json += "\"factory_unit\": \"" + String(payload.unit) + "\",";
        json += "\"table_no\": \"" + String(payload.tableNo) + "\",";
        json += "\"stage_name\": \"" + String(payload.stageName) + "\",";
        json += "\"stage_id\": " + String(payload.stage) + ",";
        json += "\"stage_count\": " + String(payload.stageCount) + ",";
        json += "\"total_count\": " + String(payload.totalCount);
        json += "}";

        Serial.printf("[ERP] Outgoing: %s\n", json.c_str());
        int httpResponseCode = http.POST(json);
        if (httpResponseCode > 0) {
          Serial.printf("[ERP] Success. Code: %d\n", httpResponseCode);
        } else {
          Serial.printf("[ERP] Failed. Error: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        http.end();
      }
    }
  }
}

void postDataToERPNext(String scanData) {
  if (webLogQueue == NULL) return;
  WebLogPayload payload;
  memset(&payload, 0, sizeof(payload));

  strncpy(payload.scannedCode, scanData.c_str(), sizeof(payload.scannedCode) - 1);
  strncpy(payload.unit, currentUnit.c_str(), sizeof(payload.unit) - 1);
  strncpy(payload.tableNo, currentTable.c_str(), sizeof(payload.tableNo) - 1);
  strncpy(payload.stageName, getStageName(), sizeof(payload.stageName) - 1);
  payload.stage = (int)currentStage;
  payload.stageCount = stageCount;
  payload.totalCount = totalCount;

  DateTime now = rtc.now();
  snprintf(payload.scanTime, sizeof(payload.scanTime), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  xQueueSend(webLogQueue, &payload, 0);
}

void triggerBuzzer() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
}

void handleScanData(String scanData) {
  triggerBuzzer();
  Serial.println("Scanned Code: " + scanData);

  if (currentStage == STAGE_IDLE) {
    Serial.println("Scan ignored: System Idle.");
    return; 
  }

  lastScannedCode = scanData;
  lastScanTime = millis();
  stageCount++;

  if (currentStage == STAGE_BUNDLING) {
    totalCount++;
  }

  // Switch to the temporary visual scan screen
  currentScreen = SCREEN_WORK_SCAN;
  drawScreen();
  postDataToERPNext(scanData); 
}

const char* getStageName() {
  switch(currentStage) {
    case STAGE_BUNDLING: return "Bundling";
    case STAGE_Q_CHECK: return "Q. Check";
    case STAGE_REPLACE: return "Replace";
    case STAGE_S_INPUT: return "S. Input";
    default: return "Idle";
  }
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 5, color);
  tft.drawRoundRect(x, y, w, h, 5, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM); // Middle-Center
  tft.drawString(label, x + (w / 2), y + (h / 2), 2);
}

// Redraws the entire screen based on current state
void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  
  // Header Frame
  tft.fillRect(0, 0, 320, 25, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM); // Top-Left
  tft.drawString("Knit Vision v2.0", 5, 5, 2);
  
  // Force time update
  lastTimeStr = "";
  updateHeaderTime();

  tft.setTextDatum(TL_DATUM);

  if (currentScreen == SCREEN_EMPTY) {
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("SYSTEM IDLE", 100, 60, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Unit: " + currentUnit, 120, 100, 2);
    tft.drawString("Table: " + currentTable, 120, 120, 2);
    
    drawButton(btnStartX, btnY, btnStartW, btnH, "START", TFT_DARKGREEN);
  }
  else if (currentScreen == SCREEN_WORKING) {
    tft.setTextColor(TFT_CYAN);
    tft.drawString("Process: " + String(getStageName()), 10, 40, 4);
    
    // Draw static labels for data
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawString("Table: " + currentTable + " | " + currentUnit, 10, 80, 2);
    
    // Call dynamic update to fill in the numbers
    updateDynamicData();

    // Draw Bottom Navigation Buttons
    drawButton(btnBackX, btnY, btnBackW, btnH, "< BACK", TFT_NAVY);
    drawButton(btnStopX, btnY, btnStopW, btnH, "STOP", TFT_MAROON);
    drawButton(btnNextX, btnY, btnNextW, btnH, "NEXT >", TFT_NAVY);
  }
  else if (currentScreen == SCREEN_WORK_SCAN) {
    tft.fillRect(0, 30, 320, 150, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SUCCESS!", 160, 60, 4);
    
    tft.setTextColor(TFT_YELLOW);
    tft.drawString(lastScannedCode, 160, 100, 4);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Stage: " + String(getStageName()), 160, 140, 2);
  }
  else if (currentScreen == SCREEN_CONFIRM_STOP) {
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("END PROCESS?", 160, 80, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("This will reset all counts.", 160, 120, 2);

    drawButton(btnYesX, btnY, btnYesW, btnH, "CONFIRM", TFT_RED);
    drawButton(btnNoX, btnY, btnNoW, btnH, "CANCEL", TFT_DARKGREY);
  }
}

void updateDynamicData() {
  if (currentScreen != SCREEN_WORKING) return;

  // Set background color same as screen background (TFT_BLACK) to overwrite old text instantly
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK); 

  String stageStr = "Stage Count: " + String(stageCount) + "    ";
  tft.drawString(stageStr, 10, 110, 4);

  String totalStr = "Total Bundles: " + String(totalCount) + "    ";
  tft.drawString(totalStr, 10, 140, 4);
}

void updateHeaderTime() {
  DateTime now = rtc.now();
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  String newTime = String(timeStr);

  if (newTime != lastTimeStr) {
    lastTimeStr = newTime;
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY); // Draw over the dark grey header bar
    tft.setTextDatum(TR_DATUM); // Top-Right
    tft.drawString(newTime, 315, 5, 2);
  }
}

void handleTouch() {
  uint16_t x, y;
  // Read touch data (Threshold 600)
  bool touched = tft.getTouch(&x, &y, 600); 

  if (touched) {
    // Print raw coordinates to the Serial Monitor so you can verify hardware connection!
    Serial.printf("[TOUCH] X: %d | Y: %d\n", x, y);
  }

  if (touched && (millis() - lastTouchTime > TOUCH_DEBOUNCE)) {
    lastTouchTime = millis();
    lastScanTime = millis(); // Reset inactivity timer on touch

    // --- IDLE SCREEN TOUCH ---
    if (currentScreen == SCREEN_EMPTY) {
      if (x > btnStartX && x < btnStartX + btnStartW && y > btnY && y < btnY + btnH) {
        currentStage = STAGE_BUNDLING;
        stageCount = 0;
        totalCount = 0;
        currentScreen = SCREEN_WORKING;
        drawScreen();
      }
    }
    // --- WORKING SCREEN TOUCH ---
    else if (currentScreen == SCREEN_WORKING) {
      // BACK Button
      if (x > btnBackX && x < btnBackX + btnBackW && y > btnY && y < btnY + btnH) {
        if (currentStage > STAGE_BUNDLING) {
          currentStage = static_cast<ProcessStage>(currentStage - 1);
          stageCount = 0; 
          drawScreen();
        }
      }
      // STOP Button
      else if (x > btnStopX && x < btnStopX + btnStopW && y > btnY && y < btnY + btnH) {
        currentScreen = SCREEN_CONFIRM_STOP;
        drawScreen();
      }
      // NEXT Button
      else if (x > btnNextX && x < btnNextX + btnNextW && y > btnY && y < btnY + btnH) {
        if (currentStage < STAGE_S_INPUT) {
          currentStage = static_cast<ProcessStage>(currentStage + 1);
          stageCount = 0;
          drawScreen();
        } else {
          // If on last stage, go to confirm stop
          currentScreen = SCREEN_CONFIRM_STOP;
          drawScreen();
        }
      }
    }
    // --- CONFIRM STOP SCREEN TOUCH ---
    else if (currentScreen == SCREEN_CONFIRM_STOP) {
      // YES / CONFIRM
      if (x > btnYesX && x < btnYesX + btnYesW && y > btnY && y < btnY + btnH) {
        currentStage = STAGE_IDLE;
        stageCount = 0;
        totalCount = 0;
        currentScreen = SCREEN_EMPTY;
        drawScreen();
      }
      // NO / CANCEL
      else if (x > btnNoX && x < btnNoX + btnNoW && y > btnY && y < btnY + btnH) {
        currentScreen = SCREEN_WORKING;
        drawScreen();
      }
    }
    // --- WORK SCAN SCREEN TOUCH ---
    else if (currentScreen == SCREEN_WORK_SCAN) {
       // Tapping the screen instantly dismisses the success popup
       currentScreen = SCREEN_WORKING;
       drawScreen();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give Serial monitor time to connect
  Serial.println("\n--- KNIT VISION BOOTING ---");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // 1. Init TFT (Pure TFT_eSPI)
  Serial.println("[1] Initializing TFT Display...");
  delay(100); // Allow serial to flush before potential crash

  Serial.println(" -> Calling tft.begin()...");
  tft.begin();
  
  Serial.println(" -> Setting rotation...");
  tft.setRotation(1); // 1 = Landscape (320x240)
  
  Serial.println(" -> Setting touch calibration...");
  uint16_t calData[5] = { 275, 3620, 264, 3532, 1 }; // Your existing calibration data
  tft.setTouch(calData);

  Serial.println(" -> Drawing initial screen...");
  // Draw initial screen
  drawScreen();
  Serial.println("[1] Screen Draw Command Sent Successfully!");

  // 2. Init WiFi & NTP
  Serial.print("[2] Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  // WAIT for WiFi to connect (Timeout after 10 seconds)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  timeClient.begin();

  // 3. Init DS3231 RTC
  Wire.begin(I2C_SDA, I2C_SCL);
  if (rtc.begin(&Wire)) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[NTP] Updating time from Internet...");
      timeClient.update();
      rtc.adjust(DateTime(timeClient.getEpochTime()));
    } else {
      Serial.println("[NTP] WiFi failed. Using local RTC time.");
    }
  } else {
    Serial.println("[RTC] Failed to initialize DS3231.");
  }

  // 4. Init USB Host (QR Scanner)
  usbHost.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.pressed) {
      if (event.ascii == '\n' || event.ascii == '\r') {
        if (qrBuffer.length() > 0) {
          finalScanData = qrBuffer;
          newScanReady = true; 
          qrBuffer = "";
        }
      } 
      else if (event.ascii >= 32 && event.ascii <= 126) {
        qrBuffer += (char)event.ascii;
      }
    }
  });
  usbHost.begin();
  Serial.println("[4] USB Scanner Host Initialized.");

  // 5. Init FreeRTOS Queue for ERPNext
  webLogQueue = xQueueCreate(20, sizeof(WebLogPayload));
  xTaskCreatePinnedToCore(webLogTask, "webLogTask", 8192, NULL, 1, NULL, 1);
  Serial.println("[5] ERP Task Started. System Ready.");
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Handle UI Input & Dynamic Updates
  handleTouch();

  if (currentMillis - lastRTCUpdate > 1000) {
    lastRTCUpdate = currentMillis;
    updateHeaderTime();
    
    // Safely update counts/data on screen without redrawing everything
    if (currentScreen == SCREEN_WORKING) {
      updateDynamicData(); 
    }
  }

  // 2. Handle Temporary Scan Screen Timeout
  if (currentScreen == SCREEN_WORK_SCAN) {
    if (currentMillis - lastScanTime > INACTIVITY_TIMEOUT) {
      currentScreen = SCREEN_WORKING;
      drawScreen(); // Restore normal working UI
    }
  }

  // 3. Handle USB Scans securely on main thread
  if (newScanReady) {
    newScanReady = false;
    handleScanData(finalScanData);
  }
}