#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_PN532.h>

// VSPI Pins (Display & Touch)
#define TFT_CS    5
#define TFT_DC    17
#define TFT_RST   16
#define TOUCH_CS   4
// #define TOUCH_IRQ 22

// HSPI Pins (PN532)
#define PN532_SCK 14
#define PN532_MISO 12
#define PN532_MOSI 13
#define PN532_SS   27
// Initialize Display and Touch (Defaults to VSPI: 18, 19, 23)
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);

// Create a custom SPIClass instance for HSPI
SPIClass hspi(HSPI);

// Initialize PN532 passing the custom HSPI bus
Adafruit_PN532 nfc(PN532_SS, &hspi);

// Touch Calibration & Thresholds
#define MIN_PRESSURE 10
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// Button Geometry
const int btnX = 60;
const int btnY = 160;
const int btnW = 200;
const int btnH = 60;

void setup() {
  Serial.begin(115200);

  // 1. Initialize Default VSPI for TFT and Touch
  ts.begin();
  ts.setRotation(1);
  
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  // 2. Initialize HSPI for PN532
  hspi.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  
  // 3. Start PN532
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 NOT FOUND on HSPI!");
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("PN532 Error!");
    while (1);
  }
  
  nfc.SAMConfig();
  Serial.println("System Initialized with Dual SPI Buses!");
  
  // Screen Title Header
  tft.fillRect(0, 0, 320, 35, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(45, 8);
  tft.print("RFID SYSTEM TEST");

  // Setup UI and Buttons
  tft.fillRect(10, 45, 300, 100, ILI9341_DARKCYAN);
  tft.drawRect(10, 45, 300, 100, ILI9341_WHITE);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 55);
  tft.print("Touch anywhere to test");

  // Draw Interactive Button
  drawButton(false);
}

void drawButton(bool pressed) {
  uint16_t color = pressed ? ILI9341_DARKGREEN : ILI9341_GREEN;
  tft.fillRoundRect(btnX, btnY, btnW, btnH, 10, color);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 10, ILI9341_WHITE);
  
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(btnX + 40, btnY + 20);
  tft.print("SCAN CARD");
}

void scanRFID() {
  tft.fillRect(10, 45, 300, 100, ILI9341_DARKCYAN);
  tft.drawRect(10, 45, 300, 100, ILI9341_WHITE);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(20, 55);
  tft.print("Scanning...");
  
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  // Poll for target card
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1500);

  tft.fillRect(10, 45, 300, 100, ILI9341_DARKCYAN);
  tft.drawRect(10, 45, 300, 100, ILI9341_WHITE);

  if (success) {
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(20, 55);
    tft.print("Card Found!");
    tft.setCursor(20, 85);
    tft.setTextColor(ILI9341_WHITE);
    tft.print("UID: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) tft.print("0");
      tft.print(uid[i], HEX);
    }
  } else {
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(20, 55);
    tft.print("No Card Detected");
  }
  delay(1500);
  
  // Reset Status
  tft.fillRect(10, 45, 300, 100, ILI9341_DARKCYAN);
  tft.drawRect(10, 45, 300, 100, ILI9341_WHITE);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(20, 55);
  tft.print("Touch anywhere to test");
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();

    if (p.z > MIN_PRESSURE) {
      // Map coordinates
      int touchX = map(p.x, TS_MINX, TS_MAXX, 0, 320);
      int touchY = map(p.y, TS_MINY, TS_MAXY, 0, 240);
      
      // Constrain to screen size
      touchX = constrain(touchX, 0, 320);
      touchY = constrain(touchY, 0, 240);

      // DEBUG OUTPUT: Print coordinates to Serial
      Serial.print("Raw X: "); Serial.print(p.x);
      Serial.print(" Raw Y: "); Serial.print(p.y);
      Serial.print(" Z: "); Serial.print(p.z);
      Serial.print(" -> Mapped X: "); Serial.print(touchX);
      Serial.print(" Y: "); Serial.println(touchY);

      // Visual Debug: Print mapping to screen
      tft.fillRect(12, 115, 296, 25, ILI9341_DARKCYAN); 
      tft.setTextColor(ILI9341_WHITE);
      tft.setTextSize(1);
      tft.setCursor(20, 120);
      tft.print("X:"); tft.print(touchX);
      tft.print(" Y:"); tft.print(touchY);
      tft.print(" R_X:"); tft.print(p.x);
      tft.print(" R_Y:"); tft.print(p.y);

      // Visual Debug: Draw a tiny red dot where touched
      tft.fillCircle(touchX, touchY, 2, ILI9341_RED);

      // Check Button Press
      if (touchX >= btnX && touchX <= (btnX + btnW) &&
          touchY >= btnY && touchY <= (btnY + btnH)) {
        drawButton(true);
        scanRFID();
        drawButton(false);
      }
    }
  }
}