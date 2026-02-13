#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\FirmwarePro.ino"
/*
 * FirmwarePro.ino
 * Merged Firmware: GPSDebug Backend + Menu UI
 *
 * Features:
 * - GPS/GNSS (Sim7600)
 * - PMS5003 + SHT31 + SDS198 Sensors
 * - SD Card Logging
 * - HTTP Streaming
 * - OLED UI with Menus (U8g2)
 * - Navigation: OneButton (BTN1=Cycle, BTN2=Select/Hold)
 * - WiFi AP for File Management
 */

// Modem definition must be before include
#define TINY_GSM_MODEM_SIM7600

#include "Adafruit_SHT31.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <OneButton.h>
#include <Preferences.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <TinyGsmClient.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_task_wdt.h>

// -------------------- DEFINES & GLOBALS --------------------
// Modem modem
#define TINY_GSM_RX_BUFFER 4096
#define SerialAT Serial1
TinyGsm modem(SerialAT);

// --- FIX: Missing Constants ---
#define SD_SAVE_DISPLAY_MS 2000
#define RTC_PROBE_PERIOD_MS 60000
#define RTC_SYNC_THRESHOLD 30
#define MIN_VALID_EPOCH 1672531200 // 2023-01-01

// SDS198 Protocol Constants
const byte HEADER = 0xAA;
const byte CMD = 0xB4;
const byte TAIL = 0xAB;

// Version
String VERSION = "Pro V0.0.1";

// Pins
#define UART_BAUD 115200
#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define MODEM_DTR 32
#define MODEM_FLIGHT 25

#define pms_TX 5
#define pms_RX 18

// #define SDS198RX_PIN 39
// #define SDS198TX_PIN 0
//  Note: SDS198 uses Serial2 (HardwareSerial)

#define BAT_PIN 35
#define NEOPIX_PIN 12
#define NUMPIXELS 1

#define BUTTON_PIN_1 19
#define BUTTON_PIN_2 23
#define POWER_PIN 33

// Objects
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
RTC_DS3231 rtc;
Preferences prefs;
SPIClass spiSD(HSPI);
WebServer server(80); // Used in wifi.ino

// PMS
SoftwareSerial pms(pms_TX, pms_RX);

// OneButton
OneButton btn1(BUTTON_PIN_1, true);
OneButton btn2(BUTTON_PIN_2, true);

// Global Variables
bool rtcOK = false;
bool SHT31OK = false;
bool SDOK = false;
bool loggingEnabled = false;
bool streaming = false;
bool wifiModeActive = false;
bool hasRed = false;

#include "config.h"

// Config Instance
SystemConfig config;

// Data Variables
uint16_t PM1 = 0, PM25 = 0, PM10 = 0;
float pmsTempC = NAN, pmsHum = NAN;
int SDS198PM100 = 0;
float tempsht31 = NAN, humsht31 = NAN;
float rtcTempC = NAN;
float batV = 0.0f;
int csq = 0;
bool networkError = false;

// GPS Data
String gpsLat = "NaN", gpsLon = "NaN";
String gpsTime = "N/A", gpsDate = "N/A";
String satellitesStr = "0", hdopStr = "N/A", gpsAlt = "N/A";
String gpsStatus = "NoFix";
String gpsSpeedKmh = "0.0";

// Internal Logic Variables
String csvFileName = "";
String logFilePath = "";
String failedTxPath = "";
// Variables moved to main for centralization
String lastSavedCSVLine = ""; // Used in sd_card.ino for OLED display
File uploadFile;              // Used in wifi.ino for file uploads

String deviceID = "/HIRIP";
const char *DEVICE_ID_STR =
    "1"; // el 06 gatilla accioes especiales como el sensor sds198
String AP_SSID_STR = "";
const char *AP_PASSWORD = "12345678";
String apIpStr = "0.0.0.0";
// -------------------- Measurements API (real endpoint) --------------------
const char *API_BASE = "http://api-sensores.cmasccp.cl/insertarMedicion";
// Must match backend exactly:
const char *IDS_SENSORES = "401,401,401,401,401,402,402,402,402,402,403,404,"
                           "405,405,405,405,405"; // sensor
                                                  // 1
// const char* IDS_SENSORES =
// "406,406,406,406,406,407,407,407,407,407,408,409,410,410,410,410,410";
// //sensor 2
// const char* IDS_SENSORES =
// "415,415,415,415,415,416,416,416,416,416,417,418,419,419,419,419,419,420,420";
// //sensor 3 //tiene sht31 const char* IDS_SENSORES =
// "448,448,448,448,448,449,449,449,449,449,450,451,452,452,452,452,452,453,453";
// //sensor 4   // tiene sds198 const char* IDS_SENSORES =
// "454,454,454,454,454,455,455,455,455,455,456,457,458,458,458,458,458,459,459";
// //sensor 5   // tiene sds198 const char* IDS_SENSORES =
// "460,460,460,460,460,461,461,461,461,461,462,463,464,464,464,464,464,467";
// //sensor 6   // tiene un sensor SDS198 const char* IDS_SENSORES =
// "468,468,468,468,468,469,469,469,469,469,470,471,472,472,472,472,472";
// //sensor 7 const char* IDS_SENSORES =
// "478,478,478,478,478,479,479,479,479,479,480,481,482,482,482,482,482,483,483";
// //sensor 9 no usado const char* IDS_SENSORES =
// "484,484,484,484,484,485,485,485,485,485,486,487,488,488,488,488,488,489,489";
// //sensor 10 no usado

const char *IDS_VARIABLES =
    "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44"; // los mismos datos pero
                                                   // cambia el ID-sensor cambia
                                                   // el numero de sensores
const char *IDS_VARIABLESSHT31 =
    "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,3,6"; // los mismos datos pero
                                                       // caria el ID-sensor
                                                       // cambia el numero de
                                                       // sensores
const char *IDS_VARIABLES06 =
    "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,51"; // los mismos datos pero
                                                      // caria el ID-sensor
                                                      // cambia el numero de
                                                      // sensores
                                                      //  ur format helpers
String valores;
String url;

// APN
const char apn[] = "gigsky-02";
const char gprsUser[] = "";
const char gprsPass[] = "";

// Counters
uint32_t sendCounter = 0;
uint32_t sdSaveCounter = 0;
uint32_t lastStream = 0;
uint8_t lastDayLogged = 0;
bool wasStreamingBeforeBoot = false;

// Animation Variables
int logoXOffset = -25;
int hiriXOffset = 128;
int proYOffset = 64;
const int LOGO_FINAL_X = 4;
const int HIRI_FINAL_X = 48;
const int PRO_FINAL_X = 106;
const int HIRI_FINAL_Y = 44;
const int PRO_FINAL_Y = 52;

// Watchdog
#define WDT_TIMEOUT 60
String rebootReason = "Unknown";
String networkOperator = "N/A";
String networkTech = "N/A";
String signalQuality = "0";
String registrationStatus = "N/A";

// XTRA
uint32_t lastXtraDownload = 0;
bool xtraSupported = false;
bool xtraLastOk = false;
const uint32_t XTRA_REFRESH_MS = 3UL * 24UL * 60UL * 60UL * 1000UL;

// Display State
// Display State
volatile DisplayState displayState = DISP_NORMAL;
volatile uint32_t displayStateStartTime = 0;
uint32_t lastOledActivity = 0;

// Modem Sync
uint8_t rtcModemSyncCount = 0;
uint32_t lastModemSyncAttempt = 0;
const uint32_t MODEM_SYNC_INTERVAL_MS = 600000;
const uint8_t MAX_MODEM_SYNC_COUNT = 3;
bool rtcNetSyncPending = false;
uint32_t rtcNextProbeMs = 0;

// AT Command Struct
struct AtSession {
  bool active = false;
  String expect1;
  String expect2;
  String resp;
  uint32_t deadline = 0;
} at;

// Buffer for PMS
static uint8_t pmsBuf[64];
static size_t pmsHead = 0;
static uint32_t lastPmsSeen = 0;

// Battery Averaging
const float alpha = 0.8;
float batteryVoltageAverage = 0;
static uint32_t lastBatSample = 0;
static float batSampleSum = 0;
static int batSampleCount = 0;
const int NUM_SAMPLES = 30;
const uint32_t BAT_SAMPLE_INTERVAL_MS = 5;

// Variables needed for GPS diag
uint32_t lastNmeaSeenMs = 0;
uint32_t gnssFixFirstMs = 0;
bool gnssFixReported = false;
uint8_t gsaFixType = 0;
uint8_t gsaSatsUsed = 0;
float gsaPdop = NAN, gsaHdop2 = NAN, gsaVdop = NAN;
uint16_t gsvSatsInView = 0;
float gsvSnrAvg = 0, gsvSnrMax = 0;
uint32_t gsvLastMs = 0;
float gsvSnrAcc = 0;
int gsvSnrCnt = 0;
uint32_t lastNmeaMs = 0;
uint32_t lastGgaMs = 0;
uint32_t lastFixMs = 0;
uint16_t nmeaCount1s = 0;
uint16_t nmeaRate = 0;
uint32_t nmeaRefMs = 0;
uint8_t fixQLast = 0;
bool ttffPrinted = false;
uint32_t gnssStartMs = 0;
bool haveFix = false;

// GnssDbgState moved to gps.ino

// SD Definition constants
const int SD_SCLK = 14, SD_MISO = 2, SD_MOSI = 15, SD_CS = 13;

// Extern function declarations (if needed explicitly, though linking usually
// handles it)
void loadConfig();
void applyLEDConfig();
void writeErrorLogHeader();
String generateCSVFileName();
void writeCSVHeader();
void drawAnimation(); // From animacion.ino
void startWifiApServer();
void stopWifiApServer();
void renderDisplay(); // From ui.ino
bool saveCSVData();
void checkRebootReason();
void readPMS();
bool readFrameSDS198(byte *buf);
void updatePmLed(float pm25);
void gnssBringUp();
void gnssDiagTick();
void gnssDebugPollAsync();
void gnssWatchdog();
bool atTick(bool &done, bool &ok);
bool atRun(const String &cmd, const String &expect1 = "OK",
           const String &expect2 = "ERROR", uint32_t timeout_ms = 8000);
bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms = 4000);
bool httpGet_webhook(const String &url);
bool detectAndEnableXtra();
bool downloadXtraOnce();
void downloadXtraIfDue();
void parseNMEA(const String &line);
void saveFailedTransmission(const String &url, const String &errorType);

// UI Event Handlers
extern void ui_btn1_click();
extern void ui_btn2_click();
extern void ui_btn2_hold();

// Oled Status Helper (used by wifi/main)
#line 336 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\FirmwarePro.ino"
void atBegin(const String &cmd, const String &expect1, const String &expect2, uint32_t timeout_ms);
#line 411 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\FirmwarePro.ino"
void updateNetworkInfo();
#line 445 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\FirmwarePro.ino"
void setup();
#line 614 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\FirmwarePro.ino"
void loop();
#line 43 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void splitSentence(const String &sentence, char delimiter, String fields[], int expectedFields);
#line 55 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
float convertToDecimal(String coord, String dir);
#line 68 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
static void resetGnssFlagsAfterStart();
#line 78 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void parseGSA(const String &s);
#line 96 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void parseGSV(const String &s);
#line 130 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void parseGGA(const String &s);
#line 157 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void parseRMC(const String &s);
#line 178 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void parseVTG(const String &s);
#line 210 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
bool setGnssAllWithFallback();
#line 281 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void gnssHotRestart();
#line 287 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
void gnssWarmRestart();
#line 2 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\helpers.ino"
static String safeFloatStr(float v);
#line 8 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\helpers.ino"
static String safeUIntStr(uint32_t v);
#line 10 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\helpers.ino"
static String safeIntStr(int v);
#line 12 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\helpers.ino"
static String safeGpsStr(const String &s);
#line 20 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\helpers.ino"
static String safeSatsStr(const String &s);
#line 27 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\http.ino"
bool ensurePdpAndNet();
#line 97 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\http.ino"
void parseHttpActionResponse(const String &resp, int &code, int &dataLen);
#line 110 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\http.ino"
bool httpGet_webhook(const String &fullUrl);
#line 69 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\pms.ino"
static uint8_t lerp8(uint8_t a, uint8_t b, float t);
#line 3 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\rtc.ino"
void printRtcOnce();
#line 11 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\rtc.ino"
static uint8_t monthFromAbbrev(const char *m);
#line 42 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\rtc.ino"
static uint32_t compileUnixTime();
#line 66 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\rtc.ino"
static bool getModemEpoch(uint32_t &epoch_out);
#line 109 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\rtc.ino"
void syncRtcSmart();
#line 48 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\serial_commands.ino"
void processSerialCommand();
#line 122 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
String getClockTime();
#line 132 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
int calcBatteryPercent(float v);
#line 140 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void drawBatteryDynamic(int xPos, int yPos, float v);
#line 183 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void drawHeader();
#line 214 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void drawFooterCircles(uint8_t cnt, uint8_t sel);
#line 227 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void drawSensorValue(uint8_t idx);
#line 256 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void drawMenuItemWithIcon(uint8_t depth, uint8_t idx);
#line 334 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void handleRestart();
#line 342 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void handleConfigWifi();
#line 467 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
void updateDisplayStateMachine();
#line 173 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
static String sanitizeForId(const String &s);
#line 184 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void listFiles();
#line 229 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void handleFileDownload();
#line 247 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void handleFileDelete();
#line 257 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void handleFileRename();
#line 270 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void handleFileUpload();
#line 286 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void handleDeleteAll();
#line 314 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void handleIp();
#line 316 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void handleSsid();
#line 318 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
void setupWifiRoutes();
#line 320 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\FirmwarePro.ino"
void oledStatus(const String &l1, const String &l2 = "", const String &l3 = "",
                const String &l4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 12);
  u8g2.print(l1);
  u8g2.setCursor(0, 26);
  u8g2.print(l2);
  u8g2.setCursor(0, 40);
  u8g2.print(l3);
  u8g2.setCursor(0, 54);
  u8g2.print(l4);
  u8g2.sendBuffer();
}

// AT Helper (needed in main)
void atBegin(const String &cmd, const String &expect1, const String &expect2,
             uint32_t timeout_ms) {
  modem.stream.print("AT");
  modem.stream.println(cmd);
  at.active = true;
  at.expect1 = expect1;
  at.expect2 = expect2;
  at.resp = "";
  at.deadline = millis() + timeout_ms;
}

bool atTick(bool &done, bool &ok) {
  while (SerialAT.available()) {
    String line = SerialAT.readStringUntil('\n');
    line.trim();
    if (line.isEmpty())
      continue;

    if (line.charAt(0) == '$') {
      parseNMEA(line);
      continue;
    }

    if (!at.active)
      continue;
    at.resp += line;
    at.resp += "\n";
    if (at.expect1.length() && line.indexOf(at.expect1) >= 0) {
      done = true;
      ok = true;
      at.active = false;
      return true;
    }
    if (at.expect2.length() && line.indexOf(at.expect2) >= 0) {
      done = true;
      ok = (at.expect2 == "OK");
      at.active = false;
      return true;
    }
  }
  if (at.active && millis() > at.deadline) {
    done = true;
    ok = false;
    at.active = false;
    return true;
  }
  done = false;
  ok = false;
  return false;
}

bool atRun(const String &cmd, const String &expect1, const String &expect2,
           uint32_t timeout_ms) {
  atBegin(cmd, expect1, expect2, timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok))
      break;
    delay(1);
  }
  return ok;
}

bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms) {
  atBegin(cmd, "OK", "ERROR", timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok))
      break;
    delay(1);
  }
  resp = at.resp;
  return ok;
}

void updateNetworkInfo() {
  String resp;
  if (sendAtSync("+COPS?", resp, 3000)) {
    int idx = resp.indexOf("+COPS:");
    if (idx >= 0) {
      int start = resp.indexOf('"', idx);
      int end = resp.indexOf('"', start + 1);
      if (start >= 0 && end > start) {
        networkOperator = resp.substring(start + 1, end);
      }
    }
  }
  if (sendAtSync("+COPS?", resp, 3000)) {
    if (resp.indexOf(",7") >= 0)
      networkTech = "LTE";
    else if (resp.indexOf(",2") >= 0)
      networkTech = "3G";
    else if (resp.indexOf(",0") >= 0)
      networkTech = "2G";
    else
      networkTech = "Unknown";
  }
  signalQuality = String(csq);
  if (sendAtSync("+CREG?", resp, 2000)) {
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0)
      registrationStatus = "Registered";
    else if (resp.indexOf(",2") >= 0)
      registrationStatus = "Searching";
    else
      registrationStatus = "NotRegistered";
  }
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, HIGH);

  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 50, 100)); // Blue startup
  pixels.show();

  Serial.println("\n[BOOT] FirmwarePro " + VERSION);
  checkRebootReason();

  prefs.begin("system", false);
  sendCounter = prefs.getUInt("sendCnt", 0);
  sdSaveCounter = prefs.getUInt("sdCnt", 0);
  csvFileName = prefs.getString("csvFile", "");
  wasStreamingBeforeBoot =
      prefs.getBool("streaming", false); // Important for autostart
  prefs.end();

  loadConfig();
  applyLEDConfig();

  // Create Log Paths
  logFilePath = String("/errors_h") + String(DEVICE_ID_STR) + String(".csv");
  failedTxPath = String("/failed_h") + String(DEVICE_ID_STR) + String(".csv");

  // SSID
  AP_SSID_STR = "HIRIPRO_" + String(DEVICE_ID_STR);

  // Display Init
  u8g2.begin();
  u8g2.setDisplayRotation(U8G2_R2);
  u8g2.setFont(u8g2_font_5x7_tf);
  lastOledActivity = millis();

  // Animation
  while (logoXOffset < LOGO_FINAL_X || hiriXOffset > HIRI_FINAL_X ||
         proYOffset > PRO_FINAL_Y) {
    if (logoXOffset < LOGO_FINAL_X)
      logoXOffset += 4;
    if (hiriXOffset > HIRI_FINAL_X)
      hiriXOffset -= 4;
    if (proYOffset > PRO_FINAL_Y)
      proYOffset -= 1;
    drawAnimation();
    delay(20);
  }

  // Show Version
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(58, 9, VERSION.c_str());
  u8g2.setCursor(0, 55);
  u8g2.print("ID:" + String(DEVICE_ID_STR));
  u8g2.sendBuffer();

  // PMS & SDS198
  pms.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, 39, -1); // SDS198

  // RTC
  if (!rtc.begin()) {
    Serial.println("[RTC] FAIL");
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:FAIL");
  } else {
    rtcOK = true;
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:OK");
  }
  u8g2.sendBuffer();

  // SHT31
  if (!sht31.begin(0x44)) {
    Serial.println("[SHT31] FAIL");
    SHT31OK = false;
  } else {
    Serial.println("[SHT31] OK");
    SHT31OK = true;
    u8g2.print(" SHT31:OK");
  }
  u8g2.sendBuffer();
  delay(1000);

  // BUTTONS (OneButton)
  btn1.attachClick(ui_btn1_click);
  btn2.attachClick(ui_btn2_click);
  btn2.attachLongPressStart(ui_btn2_hold);
  btn2.setPressTicks(500);

  // MODEM
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(300);
  digitalWrite(MODEM_PWRKEY, LOW);
  pinMode(MODEM_FLIGHT, OUTPUT);
  digitalWrite(MODEM_FLIGHT, HIGH);
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);

  oledStatus("MODEM", "Starting...");
  for (int i = 0; i < 3; i++) {
    while (!modem.testAT(1000)) {
      Serial.println("[MODEM] Retry...");
      digitalWrite(MODEM_PWRKEY, HIGH);
      delay(300);
      digitalWrite(MODEM_PWRKEY, LOW);
      delay(1000);
    }
  }
  oledStatus("MODEM", "OK");

  // Modem setup
  atRun("+CEDRXS=0", "OK", "ERROR", 1500);
  atRun("+CPSMS=0", "OK", "ERROR", 1500);

  // Network
  oledStatus("NET", "Attach/PDP...");
  if (!modem.waitForNetwork(60000))
    oledStatus("NET", "Attach FAIL");
  else {
    if (!modem.gprsConnect(apn, gprsUser, gprsPass))
      oledStatus("NET", "PDP FAIL");
    else
      oledStatus("NET", "PDP OK");
  }

  // XTRA
  xtraSupported = detectAndEnableXtra();
  if (xtraSupported) {
    oledStatus("XTRA", "Downloading...");
    xtraLastOk = downloadXtraOnce();
    lastXtraDownload = millis();
  }

  // GNSS
  gnssBringUp();

  // Watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  // SD Auto Mount
  if (config.sdAutoMount || wasStreamingBeforeBoot) {
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);
    if (SDOK) {
      if (wasStreamingBeforeBoot) {
        Serial.println("[BOOT] Autoresume Streaming");
        streaming = true;
        loggingEnabled = true;
        if (!SD.exists(csvFileName.c_str())) {
          csvFileName = generateCSVFileName();
          writeCSVHeader();
          prefs.begin("system", false);
          prefs.putString("csvFile", csvFileName);
          prefs.end();
        }
        writeErrorLogHeader();
      }
    }
  }

  Serial.println("[READY] Loop starting");
}

// -------------------- LOOP --------------------
bool FirstLoop = true;
void loop() {
  esp_task_wdt_reset();

  // Button Ticks
  btn1.tick();
  btn2.tick();

  if (wifiModeActive) {
    server.handleClient();
    return;
  }

  // Sensors & GNSS
  gnssWatchdog();
  gnssDiagTick();
  gnssDebugPollAsync();

  // First Loop Logic
  if (FirstLoop) {
    csq = modem.getSignalQuality();
    updateNetworkInfo();
    FirstLoop = false;
  }

  // PMS & SDS
  readPMS();
  byte frame[10];
  if (readFrameSDS198(frame)) {
    SDS198PM100 = (uint16_t)((frame[5] << 8) | frame[4]);
    Serial.print("PM100: ");
    Serial.println(SDS198PM100);
  }

  // AT Tick
  static uint32_t lastAtTick = 0;
  if (millis() - lastAtTick >= 50) {
    lastAtTick = millis();
    bool d, o;
    (void)atTick(d, o);
  }

  // Battery Sampling
  if (millis() - lastBatSample >= BAT_SAMPLE_INTERVAL_MS) {
    lastBatSample = millis();
    batSampleSum += analogRead(BAT_PIN);
    batSampleCount++;
    if (batSampleCount >= NUM_SAMPLES) {
      float rawV = (batSampleSum / NUM_SAMPLES / 4095.0f) * 3.3f * 2.0f * 1.15f;
      if (batteryVoltageAverage == 0)
        batteryVoltageAverage = rawV;
      batteryVoltageAverage =
          (alpha * rawV) + ((1.0 - alpha) * batteryVoltageAverage);
      batV = batteryVoltageAverage;
      batSampleSum = 0;
      batSampleCount = 0;
    }
  }

  // Display Update
  static uint32_t lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 100) {
    lastDisplayUpdate = millis();
    renderDisplay();
  }

  // Auto Off
  if (config.oledAutoOff &&
      (millis() - lastOledActivity > config.oledTimeout)) {
    u8g2.setPowerSave(1);
  }

  // Streaming Logic matches GPSDebug logic
  if (streaming && (millis() - lastStream >= config.httpSendPeriod)) {
    lastStream = millis();

    bool sdSaved = saveCSVData();
    if (sdSaved && loggingEnabled) {
      displayState = DISP_SD_SAVED;
      displayStateStartTime = millis();
      renderDisplay(); // Force update
      delay(200);      // Small blocking delay for feedback visibility
    }

    // Reference values
    String val;
    String url;

    String v1 = isnan(pmsTempC) ? "0" : safeFloatStr(pmsTempC);
    String v2 = isnan(pmsHum) ? "0" : safeFloatStr(pmsHum);
    String v3 = safeUIntStr(PM1);
    String v4 = safeUIntStr(PM25);
    String v5 = safeUIntStr(PM10);
    String v6 = safeGpsStr(gpsLat);
    String v7 = safeGpsStr(gpsLon);
    String v8 = safeIntStr(csq);
    String v9 = (gpsSpeedKmh.length() ? gpsSpeedKmh : "0");
    String v10 = safeSatsStr(satellitesStr);
    String v11 = safeFloatStr(rtcTempC);
    String v12 = safeFloatStr(batV);
    String v13 = v6; // Lat
    String v14 = v7; // Lon
    String v15 = String(DEVICE_ID_STR);
    String v16 = safeUIntStr(sendCounter + 1);
    String v17 = loggingEnabled ? "1" : "0";

    // Build URL Logic
    if (SHT31OK == true) {
      const String v18 = isnan(tempsht31) ? "0" : safeFloatStr(tempsht31);
      const String v19 = isnan(humsht31) ? "0" : safeFloatStr(humsht31);
      val = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," +
            v7 + "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," +
            v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17 + "," + v18 +
            "," + v19;
      url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES +
            "&idsVariables=" + IDS_VARIABLESSHT31 + "&valores=" + val;
    } else if (String(DEVICE_ID_STR) == "06") {
      const String v18 = safeUIntStr(SDS198PM100);
      val = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," +
            v7 + "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," +
            v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17 + "," + v18;
      url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES +
            "&idsVariables=" + IDS_VARIABLES06 + "&valores=" + val;
    } else {
      val = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," +
            v7 + "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," +
            v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17;
      url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES +
            "&idsVariables=" + IDS_VARIABLES + "&valores=" + val;
    }

    Serial.println("[HTTP] GET " + url);
    // Transmit
    if (httpGet_webhook(url)) {
      sendCounter++;
      // Persist
      prefs.begin("system", false);
      prefs.putUInt("sendCnt", sendCounter);
      prefs.putUInt("sdCnt", sdSaveCounter);
      prefs.end();
      Serial.println("[HTTP] OK");
    } else {
      saveFailedTransmission(url, "HTTP_FAIL");
      Serial.println("[HTTP] FAIL");
    }
  }

  // Serial Commands
  processSerialCommand();
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\animacion.ino"
#define LOGO_WIDTH 40   // 1/3 del tamaño original
#define LOGO_HEIGHT 34  // 1/3 del tamaño original

const unsigned char logo_bits[] PROGMEM = {
	0x00, 0x0f, 0x00, 0x3c, 0x00, 0x80, 0x1f, 0x00, 0x7e, 0x00, 0xc0, 0x3f, 0x00, 0xff, 0x00, 0xe0, 
	0x7f, 0x80, 0xff, 0x01, 0xe0, 0xff, 0xff, 0xff, 0x01, 0xe0, 0xff, 0xff, 0xff, 0x01, 0xe0, 0x7f, 
	0x80, 0xff, 0x01, 0xc0, 0x3f, 0x00, 0xff, 0x00, 0x80, 0x1f, 0x00, 0x7e, 0x00, 0x00, 0x0f, 0x00, 
	0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x0f, 0x00, 
	0x3f, 0x7e, 0x80, 0x1f, 0x80, 0x7f, 0xff, 0xc0, 0x3f, 0xc0, 0xf3, 0xff, 0xe1, 0x7f, 0xc0, 0xf3, 
	0xff, 0xe1, 0xff, 0xff, 0xc0, 0xff, 0xe1, 0xff, 0xff, 0xc0, 0xff, 0xe1, 0x7f, 0xc0, 0xf3, 0xff, 
	0xc0, 0x3f, 0xc0, 0xf3, 0x7e, 0x80, 0x1f, 0x80, 0x7f, 0x3c, 0x00, 0x0f, 0x00, 0x3f, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x3c, 0x00, 0x80, 0x1f, 0x00, 
	0x7e, 0x00, 0xc0, 0x3f, 0x00, 0xff, 0x00, 0xe0, 0x7f, 0x80, 0xff, 0x01, 0xe0, 0xff, 0xff, 0xff, 
	0x01, 0xe0, 0xff, 0xff, 0xff, 0x01, 0xe0, 0x7f, 0x80, 0xff, 0x01, 0xc0, 0x3f, 0x00, 0xff, 0x00, 
	0x80, 0x1f, 0x00, 0x7e, 0x00, 0x00, 0x0f, 0x00, 0x3c, 0x00
};



void drawAnimation() {
  u8g2.clearBuffer();
  
  // Dibujar logo escalado
  //u8g2.setDrawColor(0);  // Establece el color de dibujo a negro
  u8g2.drawXBMP(logoXOffset, 10, LOGO_WIDTH, LOGO_HEIGHT, logo_bits);
  //u8g2.setDrawColor(1);  // Establece el color de dibujo a blanco
  
  // Dibujar "HIRI" más grande
  u8g2.setFont(u8g2_font_fub30_tr);  // Fuente aún más grande para HIRI
  u8g2.drawStr(hiriXOffset, HIRI_FINAL_Y, "HIRI");
  
  // Dibujar "PRO" más pequeño, usando las variables de posición final
  u8g2.setFont(u8g2_font_6x10_tr);  // Fuente muy pequeña para PRO
  u8g2.drawStr(PRO_FINAL_X, proYOffset, "PRO");  // Usa PRO_FINAL_X para la posición horizontal
  
  u8g2.sendBuffer();
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\config.ino"
// -------------------- Configuration System --------------------
// Sistema de configuración persistente en flash (Preferences)
// Configuración accesible por comandos seriales
// NOTA: struct SystemConfig y variable config están declaradas en el archivo
// principal

// -------------------- Load Configuration --------------------
void loadConfig() {
  prefs.begin("config", false);

  // SD
  config.sdAutoMount = prefs.getBool("sdAuto", false);
  config.sdSavePeriod = prefs.getUInt("sdSavePer", 3000);

  // HTTP
  config.httpSendPeriod = prefs.getUInt("httpPer", 3000);
  config.httpTimeout = prefs.getUShort("httpTO", 15);

  // Display
  config.oledAutoOff = prefs.getBool("oledOff", false);
  config.oledTimeout = prefs.getUInt("oledTO", 120000);

  // Power
  config.ledEnabled = prefs.getBool("ledEn", true);
  config.ledBrightness = prefs.getUChar("ledBr", 50);

  // Autostart
  config.autostart = prefs.getBool("autoStart", false);
  config.autostartWaitGps = prefs.getBool("autoGPS", false);
  config.autostartGpsTimeout = prefs.getUShort("autoGPSTO", 600);

  // GNSS Mode
  config.gnssMode = prefs.getUChar("gnssMode", 15);

  prefs.end();

  Serial.println("[CONFIG] Loaded from flash");
  Serial.printf("[CONFIG] SD auto-mount: %s, save period: %lums\n",
                config.sdAutoMount ? "ON" : "OFF", config.sdSavePeriod);
  Serial.printf("[CONFIG] HTTP send period: %lums, timeout: %us\n",
                config.httpSendPeriod, config.httpTimeout);
  Serial.printf("[CONFIG] OLED auto-off: %s, timeout: %lums\n",
                config.oledAutoOff ? "ON" : "OFF", config.oledTimeout);
  Serial.printf("[CONFIG] LED enabled: %s, brightness: %u%%\n",
                config.ledEnabled ? "YES" : "NO", config.ledBrightness);
  Serial.printf("[CONFIG] Autostart: %s, wait GPS: %s, GPS timeout: %us\n",
                config.autostart ? "YES" : "NO",
                config.autostartWaitGps ? "YES" : "NO",
                config.autostartGpsTimeout);
  Serial.printf("[CONFIG] GNSS mode: %u\n", config.gnssMode);
}

// -------------------- Save Configuration --------------------
void saveConfig() {
  prefs.begin("config", false);

  // SD
  prefs.putBool("sdAuto", config.sdAutoMount);
  prefs.putUInt("sdSavePer", config.sdSavePeriod);

  // HTTP
  prefs.putUInt("httpPer", config.httpSendPeriod);
  prefs.putUShort("httpTO", config.httpTimeout);

  // Display
  prefs.putBool("oledOff", config.oledAutoOff);
  prefs.putUInt("oledTO", config.oledTimeout);

  // Power
  prefs.putBool("ledEn", config.ledEnabled);
  prefs.putUChar("ledBr", config.ledBrightness);

  // Autostart
  prefs.putBool("autoStart", config.autostart);
  prefs.putBool("autoGPS", config.autostartWaitGps);
  prefs.putUShort("autoGPSTO", config.autostartGpsTimeout);

  // GNSS Mode
  prefs.putUChar("gnssMode", config.gnssMode);

  prefs.end();

  Serial.println("[CONFIG] Saved to flash");
}

// -------------------- Reset to Defaults --------------------
void configSetDefaults() {
  config.sdAutoMount = false; // NO montar en boot
  config.sdSavePeriod = 3000; // 3 segundos

  config.httpSendPeriod = 3000; // 3 segundos
  config.httpTimeout = 15;      // 15 segundos

  config.oledAutoOff = false;  // Siempre encendida
  config.oledTimeout = 120000; // 2 minutos

  config.ledEnabled = true;  // LED habilitado
  config.ledBrightness = 50; // 50% brillo

  config.autostart = false;         // NO autostart
  config.autostartWaitGps = false;  // NO esperar GPS
  config.autostartGpsTimeout = 600; // 10 minutos (default)

  config.gnssMode = 15; // Todas las constelaciones (default)

  Serial.println("[CONFIG] Reset to defaults");
}

// -------------------- Print Configuration --------------------
void printConfig() {
  Serial.println("=== HIRI PRO Configuration ===");

  Serial.println("\n[SD Card]");
  Serial.printf("  Auto-mount on boot: %s\n",
                config.sdAutoMount ? "ON" : "OFF");
  Serial.printf("  Save period:        %lu ms (%lu s)\n", config.sdSavePeriod,
                config.sdSavePeriod / 1000);

  Serial.println("\n[HTTP Transmission]");
  Serial.printf("  Send period:        %lu ms (%lu s)\n", config.httpSendPeriod,
                config.httpSendPeriod / 1000);
  Serial.printf("  Timeout:            %u seconds\n", config.httpTimeout);

  Serial.println("\n[Display OLED]");
  Serial.printf("  Auto-off:           %s\n",
                config.oledAutoOff ? "ON" : "OFF");
  Serial.printf("  Timeout:            %lu ms (%lu s)\n", config.oledTimeout,
                config.oledTimeout / 1000);

  Serial.println("\n[Power/LED]");
  Serial.printf("  NeoPixel enabled:   %s\n", config.ledEnabled ? "YES" : "NO");
  Serial.printf("  Brightness:         %u%%\n", config.ledBrightness);

  Serial.println("\n[Autostart]");
  Serial.printf("  Autostart:          %s\n", config.autostart ? "YES" : "NO");
  Serial.printf("  Wait for GPS fix:   %s\n",
                config.autostartWaitGps ? "YES" : "NO");
  Serial.printf("  GPS timeout:        %u seconds (%u min)\n",
                config.autostartGpsTimeout, config.autostartGpsTimeout / 60);

  Serial.println("\n[GNSS Configuration]");
  Serial.printf("  Mode:               %u ", config.gnssMode);
  switch (config.gnssMode) {
  case 1:
    Serial.println("(GPS only)");
    break;
  case 3:
    Serial.println("(GPS + GLONASS)");
    break;
  case 5:
    Serial.println("(GPS + BEIDOU)");
    break;
  case 7:
    Serial.println("(GPS + GLONASS + BEIDOU)");
    break;
  case 15:
    Serial.println("(ALL: GPS + GLONASS + GALILEO + BEIDOU)");
    break;
  default:
    Serial.println("(Unknown)");
    break;
  }

  Serial.println();
}

// -------------------- Print SD Info --------------------
void printSDInfo() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted");
    return;
  }

  Serial.println("=== SD Card Info ===");
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
  uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
  Serial.printf("Card size:    %llu MB\n", cardSize);
  Serial.printf("Total:        %llu MB\n", totalBytes);
  Serial.printf("Used:         %llu MB\n", usedBytes);
  Serial.printf("Free:         %llu MB\n", totalBytes - usedBytes);
  Serial.printf("Current file: %s\n", csvFileName.c_str());
}

// -------------------- Print SD File List --------------------
void printSDFileList() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted");
    return;
  }

  Serial.println("=== SD Files ===");
  File root = SD.open("/");
  if (!root) {
    Serial.println("[SD] Cannot open root");
    return;
  }

  File file = root.openNextFile();
  int count = 0;
  while (file) {
    if (!file.isDirectory()) {
      Serial.printf("  %s - %lu bytes\n", file.name(),
                    (unsigned long)file.size());
      count++;
    }
    file = root.openNextFile();
  }
  Serial.printf("Total: %d files\n", count);
}

// -------------------- Apply LED Configuration --------------------
void applyLEDConfig() {
  if (!config.ledEnabled) {
    pixels.clear();
    pixels.show();
    Serial.println("[LED] Disabled by config");
  } else {
    // Convertir porcentaje a 0-255
    uint8_t brightness = map(config.ledBrightness, 0, 100, 0, 255);
    pixels.setBrightness(brightness);
    Serial.printf("[LED] Brightness set to %u%% (%u/255)\n",
                  config.ledBrightness, brightness);
  }
}

// -------------------- Clear SD Card --------------------
void clearSDCard() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted - cannot clear");
    return;
  }

  Serial.println("[SD] Starting to delete all files...");
  File root = SD.open("/");
  if (!root) {
    Serial.println("[SD] Cannot open root directory");
    return;
  }

  int deletedCount = 0;
  int failedCount = 0;

  // First pass: collect all filenames
  String fileNames[50]; // Max 50 files
  int fileCount = 0;

  File file = root.openNextFile();
  while (file && fileCount < 50) {
    if (!file.isDirectory()) {
      fileNames[fileCount++] = String("/") + String(file.name());
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  // Second pass: delete all collected files
  for (int i = 0; i < fileCount; i++) {
    Serial.print("[SD] Deleting: ");
    Serial.print(fileNames[i]);

    if (SD.remove(fileNames[i].c_str())) {
      Serial.println(" - OK");
      deletedCount++;
    } else {
      Serial.println(" - FAILED");
      failedCount++;
    }
  }

  Serial.println("[SD] Clear complete:");
  Serial.printf("  Deleted: %d files\n", deletedCount);
  if (failedCount > 0) {
    Serial.printf("  Failed:  %d files\n", failedCount);
  }

  // Reset CSV filename in preferences since all files were deleted
  csvFileName = "";
  prefs.begin("system", false);
  prefs.putString("csvFile", "");
  prefs.end();
  Serial.println("[SD] CSV filename cleared from memory");
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\gps.ino"
// -------------------- External Variables --------------------
// -------------------- External Variables --------------------
#include "config.h"
#include <TinyGsmClient.h>
extern SystemConfig config;
extern struct AtSession at;
extern TinyGsm modem;

extern String gpsLat, gpsLon, gpsDate, gpsTime, gpsStatus, gpsAlt, gpsSpeedKmh;
extern String satellitesStr, hdopStr;
extern bool haveFix, ttffPrinted;
extern uint32_t gnssStartMs, lastFixMs, lastGgaMs, lastNmeaMs, lastNmeaSeenMs;
extern uint16_t nmeaCount1s, nmeaRate;
extern uint32_t nmeaRefMs;
extern uint8_t fixQLast;

// GSA/GSV
extern uint8_t gsaFixType, gsaSatsUsed;
extern float gsaPdop, gsaHdop2, gsaVdop;
extern uint16_t gsvSatsInView;
extern float gsvSnrAvg, gsvSnrMax, gsvSnrAcc;
extern uint32_t gsvLastMs;
extern int gsvSnrCnt;

extern const char apn[];
extern const char gprsUser[];
extern const char gprsPass[];
extern bool xtraSupported, xtraLastOk;
extern uint32_t lastXtraDownload;

// GnssDbgState local to this module
enum GnssDbgState {
  GNSS_DBG_IDLE,
  GNSS_DBG_ASK_STATUS,
  GNSS_DBG_WAIT_STATUS,
  GNSS_DBG_ASK_INFO,
  GNSS_DBG_WAIT_INFO
};
GnssDbgState gnssDbgState = GNSS_DBG_IDLE;
uint32_t gnssDbgNextAt = 0;

// -------------------- GPS parsing helpers --------------------
void splitSentence(const String &sentence, char delimiter, String fields[],
                   int expectedFields) {
  int fieldIndex = 0, start = 0, end = sentence.indexOf(delimiter);
  while (end != -1 && fieldIndex < expectedFields) {
    fields[fieldIndex++] = sentence.substring(start, end);
    start = end + 1;
    end = sentence.indexOf(delimiter, start);
  }
  if (fieldIndex < expectedFields)
    fields[fieldIndex++] = sentence.substring(start);
}

float convertToDecimal(String coord, String dir) {
  int degDigits = (dir == "N" || dir == "S") ? 2 : 3;
  if ((int)coord.length() < degDigits)
    return NAN;
  int degrees = coord.substring(0, degDigits).toInt();
  float minutes = coord.substring(degDigits).toFloat();
  float dec = degrees + minutes / 60.0f;
  if (dir == "S" || dir == "W")
    dec = -dec;
  return dec;
}

// -------------------- GNSS bring-up + watchdog --------------------
static inline void resetGnssFlagsAfterStart() {
  gnssStartMs = millis();
  // Reset de estado TTFF
  gnssFixReported = false;
  ttffPrinted = false;
  haveFix = false;
  fixQLast = 0;
  lastFixMs = 0;
}

void parseGSA(const String &s) {
  // $xxGSA,mode,fixType,sv1,...,sv12,PDOP,HDOP,VDOP*CS
  const int N = 20;
  String f[N];
  splitSentence(s, ',', f, N);

  gsaFixType = f[2].toInt(); // 1/2/3
  uint8_t used = 0;
  for (int i = 3; i <= 14; ++i)
    if (f[i].length() > 0)
      used++;
  gsaSatsUsed = used;

  gsaPdop = f[15].length() ? f[15].toFloat() : NAN;
  gsaHdop2 = f[16].length() ? f[16].toFloat() : NAN;
  gsaVdop = f[17].length() ? f[17].toFloat() : NAN;
}

void parseGSV(const String &s) {
  // $xxGSV,total,msgnum,sats, (prn,elev,az,snr) x up to 4
  const int N = 20;
  String f[N];
  splitSentence(s, ',', f, N);

  int total = f[1].toInt();
  int num = f[2].toInt();
  int sats = f[3].toInt();
  if (num == 1) {
    gsvSnrAcc = 0;
    gsvSnrCnt = 0;
    gsvSnrMax = 0;
  }

  for (int i = 4; i + 3 < N; i += 4) {
    if (f[i + 3].length()) {
      int snr = f[i + 3].toInt();
      if (snr > 0) {
        gsvSnrAcc += snr;
        gsvSnrCnt++;
        if (snr > gsvSnrMax)
          gsvSnrMax = snr;
      }
    }
  }

  if (num == total || total == 0) {
    gsvSatsInView = sats;
    gsvSnrAvg = (gsvSnrCnt > 0) ? (gsvSnrAcc / gsvSnrCnt) : 0;
    gsvLastMs = millis();
  }
}

void parseGGA(const String &s) {
  const int N = 15;
  String f[N];
  splitSentence(s, ',', f, N);
  String lat = f[2], latD = f[3], lon = f[4], lonD = f[5];
  String fixQ = f[6];
  satellitesStr = f[7];
  hdopStr = f[8];
  String alt = f[9];

  lastGgaMs = millis();
  fixQLast = (uint8_t)fixQ.toInt();
  if (fixQLast >= 1) {
    haveFix = true;
    lastFixMs = lastGgaMs;
  }

  float dlat = convertToDecimal(lat, latD);
  float dlon = convertToDecimal(lon, lonD);
  if (!isnan(dlat) && !isnan(dlon)) {
    gpsLat = String(dlat, 6);
    gpsLon = String(dlon, 6);
  }
  gpsAlt = alt;
  gpsStatus = (fixQ.toInt() >= 1) ? "Fix" : "NoFix";
}

void parseRMC(const String &s) {
  const int N = 12;
  String f[N];
  splitSentence(s, ',', f, N);
  String t = f[1], st = f[2], lat = f[3], latD = f[4], lon = f[5], lonD = f[6],
         date = f[9];
  float dlat = convertToDecimal(lat, latD);
  float dlon = convertToDecimal(lon, lonD);
  if (!isnan(dlat) && !isnan(dlon)) {
    gpsLat = String(dlat, 6);
    gpsLon = String(dlon, 6);
  }
  gpsStatus = (st == "A") ? "Fix" : "NoFix";
  if (t.length() >= 6)
    gpsTime =
        t.substring(0, 2) + ":" + t.substring(2, 4) + ":" + t.substring(4, 6);
  if (date.length() == 6)
    gpsDate = date.substring(0, 2) + "/" + date.substring(2, 4) + "/20" +
              date.substring(4, 6);
}

void parseVTG(const String &s) {
  const int N = 10;
  String f[N];
  splitSentence(s, ',', f, N);
  String kmh = (f[7].length() > 0) ? f[7] : "0.0";
  gpsSpeedKmh = kmh;
}

// Talker-agnostic (GP/GN/GA/GL/BD/GB/QZ...): detecta tipo por [3..5]
void parseNMEA(const String &line) {
  lastNmeaMs = millis();
  nmeaCount1s++;
  lastNmeaSeenMs = lastNmeaMs; // heartbeat NMEA

  if (line.length() < 7 || line.charAt(0) != '$')
    return;
  String typ = line.substring(3, 6);

  if (typ == "GGA")
    parseGGA(line);
  else if (typ == "RMC")
    parseRMC(line);
  else if (typ == "VTG")
    parseVTG(line);
  else if (typ == "GSA")
    parseGSA(line);
  else if (typ == "GSV")
    parseGSV(line);
}

// -------------------- GNSS bring-up functions --------------------
// Intentar configurar el modo GNSS según config, con fallback automático
bool setGnssAllWithFallback() {
  // Intentar primero el modo configurado por el usuario
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "+CGNSSMODE=%u,1", config.gnssMode);

  if (atRun(cmd, "OK", "ERROR", 2000)) {
    Serial.print("[GNSS] Mode set: ");
    Serial.print(config.gnssMode);
    switch (config.gnssMode) {
    case 1:
      Serial.println(" (GPS only)");
      break;
    case 3:
      Serial.println(" (GPS + GLONASS)");
      break;
    case 5:
      Serial.println(" (GPS + BEIDOU)");
      break;
    case 7:
      Serial.println(" (GPS + GLONASS + BEIDOU)");
      break;
    case 15:
      Serial.println(" (ALL: GPS + GLONASS + GALILEO + BEIDOU)");
      break;
    default:
      Serial.println(" (Unknown)");
      break;
    }
    return true;
  }

  // Si falla el modo configurado, intentar fallback con modos más simples
  Serial.print("[GNSS] Configured mode ");
  Serial.print(config.gnssMode);
  Serial.println(" failed, trying fallback...");

  const char *fallbackTries[] = {
      "+CGNSSMODE=7,1", // GPS+GLONASS+BEIDOU
      "+CGNSSMODE=3,1", // GPS+GLONASS
      "+CGNSSMODE=1,1"  // GPS only
  };

  for (size_t i = 0; i < sizeof(fallbackTries) / sizeof(fallbackTries[0]);
       ++i) {
    if (atRun(fallbackTries[i], "OK", "ERROR", 2000)) {
      Serial.print("[GNSS] Fallback mode set: ");
      Serial.println(fallbackTries[i]);
      return true;
    }
  }

  Serial.println("[GNSS] All attempts to set constellation FAILED");
  return false;
}

void gnssBringUp() {
  Serial.println("[GNSS] Bring-up...");
  atRun("+CGPS=0", "OK", "ERROR", 5000);
  atRun("+CGNSSPWR=1", "OK", "ERROR", 2000);

  (void)setGnssAllWithFallback(); // multi-constelación con fallback

  atRun("+CGPSNMEA=200191", "OK", "ERROR", 2000);
  atRun("+CGPSNMEARATE=1", "OK", "ERROR", 2000);
  atRun("+CGPS=1", "OK", "ERROR", 8000);
  // NMEA Output to AT port (RMC,GGA,VTG on AT port)
  atRun("+CGPSINFOCFG=1,31", "OK", "ERROR", 2000);
  Serial.println("[GNSS] ON (NMEA 1 Hz)");
  resetGnssFlagsAfterStart();
}

void gnssHotRestart() {
  Serial.println("[GNSS] HOT restart");
  atRun("+CGPSRST=0", "OK", "ERROR", 3000);
  resetGnssFlagsAfterStart();
}

void gnssWarmRestart() {
  Serial.println("[GNSS] WARM restart");
  atRun("+CGPSRST=1", "OK", "ERROR", 3000);
  resetGnssFlagsAfterStart();
}

void gnssWatchdog() {
  if (haveFix)
    return;
  uint32_t alive = millis() - gnssStartMs;
  if (alive > 300000UL) {
    gnssWarmRestart();
  } else if (alive > 120000UL) {
    gnssHotRestart();
  }
}

// -------------------- Async GNSS diag & debug (non-blocking)
// --------------------
void gnssDiagTick() {
  const uint32_t now = millis();

  // Calcular tasa NMEA cada 1s
  if (now - nmeaRefMs >= 1000) {
    nmeaRefMs = now;
    nmeaRate = nmeaCount1s; // aprox Hz
    nmeaCount1s = 0;
  }

  // Edades (en s)
  uint32_t ageNmea = (lastNmeaMs ? (now - lastNmeaMs) / 1000UL : 9999);
  uint32_t ageGga = (lastGgaMs ? (now - lastGgaMs) / 1000UL : 9999);
  uint32_t ageFix = (lastFixMs ? (now - lastFixMs) / 1000UL : 9999);
  (void)ageFix;

  // Re-armar salida NMEA si se silenció (>5s sin NMEA)
  if (ageNmea > 5 && !at.active) {
    Serial.println("[GNSSDIAG] No NMEA >5s -> re-enable NMEA 1Hz");
    (void)atRun("+CGPSNMEA=200191", "OK", "ERROR", 1200);
    (void)atRun("+CGPSNMEARATE=1", "OK", "ERROR", 1200);
  }

  // Si >15s sin NMEA, asegura GNSS ON (idempotente; barato)
  if (ageNmea > 15 && !at.active) {
    (void)atRun("+CGNSSPWR=1", "OK", "ERROR", 1200);
    (void)atRun("+CGPS=1", "OK", "ERROR", 3000);
  }

  // Imprimir cada 5s un resumen legible (solo Serial)
  static uint32_t nextPrint = 0;
  if (now >= nextPrint) {
    nextPrint = now + 5000;
    Serial.printf("[GNSSDIAG] rate=%u Hz  ageNMEA=%lus  ageGGA=%lus  fixQ=%u  "
                  "sats=%s  HDOP=%s  status=%s\n",
                  nmeaRate, (unsigned long)ageNmea, (unsigned long)ageGga,
                  (unsigned)fixQLast, satellitesStr.c_str(), hdopStr.c_str(),
                  gpsStatus.c_str());

    // Pequeño resumen de GSA/GSV por Serial
    if (!isnan(gsaPdop) || !isnan(gsaHdop2)) {
      Serial.printf("[GSA] fixType=%u used=%u PDOP=%.1f HDOP=%.1f VDOP=%.1f\n",
                    gsaFixType, gsaSatsUsed, gsaPdop, gsaHdop2, gsaVdop);
    }
    Serial.printf("[GSV] inView=%u SNR(avg)=%.1f SNR(max)=%.1f age=%lus\n",
                  (unsigned)gsvSatsInView, gsvSnrAvg, gsvSnrMax,
                  (unsigned)((gsvLastMs ? now - gsvLastMs : 0) / 1000UL));
  }

  // TTFF (solo una vez por ciclo de encendido/restart)
  if (!ttffPrinted && haveFix && lastFixMs && gnssStartMs) {
    uint32_t ttff =
        (lastFixMs >= gnssStartMs) ? (lastFixMs - gnssStartMs) / 1000UL : 0;
    Serial.printf("[GNSS] TTFF = %lu s\n", (unsigned long)ttff);
    ttffPrinted = true;
  }
}

void gnssDebugPollAsync() {
  const uint32_t PERIOD_MS = 5000;
  if (millis() < gnssDbgNextAt)
    return;
  if (at.active)
    return;

  bool done = false, ok = false;
  (void)atTick(done, ok);

  switch (gnssDbgState) {
  case GNSS_DBG_IDLE:
    gnssDbgState = GNSS_DBG_ASK_STATUS;
    break;
  case GNSS_DBG_ASK_STATUS:
    atBegin("+CGPSSTATUS?", "OK", "ERROR", 1200);
    gnssDbgState = GNSS_DBG_WAIT_STATUS;
    break;
  case GNSS_DBG_WAIT_STATUS:
    if ((bool)atTick(done, ok)) {
      Serial.println(ok ? "[GNSS] +CGPSSTATUS? OK"
                        : "[GNSS] +CGPSSTATUS? FAIL");
      Serial.print(at.resp);
      gnssDbgState = GNSS_DBG_ASK_INFO;
    }
    break;
  case GNSS_DBG_ASK_INFO:
    atBegin("+CGPSINFO", "OK", "ERROR", 1200);
    gnssDbgState = GNSS_DBG_WAIT_INFO;
    break;
  case GNSS_DBG_WAIT_INFO:
    if ((bool)atTick(done, ok)) {
      Serial.println(ok ? "[GNSS] +CGPSINFO OK" : "[GNSS] +CGPSINFO FAIL");
      Serial.print(at.resp);
      Serial.printf(
          "[GNSS] Status=%s Sats=%s HDOP=%s Alt=%sm Spd=%skm/h Lat=%s Lon=%s\n",
          gpsStatus.c_str(), satellitesStr.c_str(), hdopStr.c_str(),
          gpsAlt.c_str(), gpsSpeedKmh.c_str(), gpsLat.c_str(), gpsLon.c_str());
      gnssDbgState = GNSS_DBG_IDLE;
      gnssDbgNextAt = millis() + PERIOD_MS;
    }
    break;
  }
}

// -------------------- XTRA / AGNSS --------------------
bool detectAndEnableXtra() {
  String r;
  if (!sendAtSync("+CGPSXE=?", r, 2000)) {
    Serial.println("[XTRA] Not supported");
    return false;
  }
  if (!sendAtSync("+CGPSXE=1", r, 2000)) {
    Serial.println("[XTRA] Enable FAIL");
    return false;
  }
  Serial.println("[XTRA] Enabled");
  return true;
}

bool downloadXtraOnce() {
  if (!modem.isGprsConnected()) {
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      Serial.println("[XTRA] PDP reconnect FAIL");
      return false;
    }
  }
  String r;
  (void)sendAtSync("+CGPSXD=?", r, 2000);
  bool ok = sendAtSync("+CGPSXD=1", r, 120000);
  Serial.println(ok ? "[XTRA] Download OK" : "[XTRA] Download FAIL");
  xtraLastOk = ok;
  return ok;
}

void downloadXtraIfDue() {
  if (!xtraSupported)
    return;
  if (millis() - lastXtraDownload < XTRA_REFRESH_MS)
    return;
  if (downloadXtraOnce())
    lastXtraDownload = millis();
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\helpers.ino"
// -------------------- Safe value helpers --------------------
static inline String safeFloatStr(float v) {
  if (isnan(v) || isinf(v))
    return "0";
  return String(v, 3);
}

static inline String safeUIntStr(uint32_t v) { return String(v); }

static inline String safeIntStr(int v) { return String(v); }

static inline String safeGpsStr(const String &s) {
  if (s.length() == 0)
    return "0";
  if (s == "NaN" || s == "N/A")
    return "0";
  return s;
}

static inline String safeSatsStr(const String &s) {
  if (s.length() == 0)
    return "0";
  for (size_t i = 0; i < s.length(); ++i) {
    if (!isDigit(s[i]))
      return "0";
  }
  return s;
}

// -------------------- Watchdog & Error Logging Helpers --------------------
void checkRebootReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
  case ESP_RST_POWERON:
    rebootReason = "PowerOn";
    break;
  case ESP_RST_SW:
    rebootReason = "Software";
    break;
  case ESP_RST_PANIC:
    rebootReason = "Panic";
    break;
  case ESP_RST_INT_WDT:
    rebootReason = "IntWatchdog";
    break;
  case ESP_RST_TASK_WDT:
    rebootReason = "TaskWatchdog";
    break;
  case ESP_RST_WDT:
    rebootReason = "OtherWatchdog";
    break;
  case ESP_RST_DEEPSLEEP:
    rebootReason = "DeepSleep";
    break;
  case ESP_RST_BROWNOUT:
    rebootReason = "Brownout";
    break;
  default:
    rebootReason = "Unknown";
    break;
  }
}

// -------------------- Error Logging --------------------
void logError(const String &type, const String &ctx, const String &msg) {
  Serial.println("[ERROR] " + type + " (" + ctx + "): " + msg);

  if (!SDOK)
    return;

  File f = SD.open(logFilePath, FILE_APPEND);
  if (f) {
    if (f.size() == 0) {
      f.println("timestamp,type,context,message");
    }
    String ts = rtcOK ? rtc.now().timestamp() : String(millis());
    f.print(ts);
    f.print(",");
    f.print(type);
    f.print(",");
    f.print(ctx);
    f.print(",");
    f.println(msg);
    f.close();
  }
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\http.ino"
// -------------------- HTTP helpers --------------------
// -------------------- External Variables --------------------
extern TinyGsm modem;
extern struct AtSession at;
extern Adafruit_NeoPixel pixels;
extern const char apn[];
extern const char gprsUser[];
extern const char gprsPass[];
extern bool hasRed;
extern float batV;
extern uint16_t PM25;
extern void updatePmLed(float pm25);
extern void logError(const String &type, const String &ctx, const String &msg);
extern bool atTick(bool &done, bool &ok);
extern bool atRun(const String &cmd, const String &expect1,
                  const String &expect2, uint32_t timeout_ms);
extern bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms);

// Variables for PDP reconnect (kept local static as they are implementation
// details)
static uint8_t pdpReconnectFailCount = 0;
static uint32_t lastPdpReconnectAttempt = 0;
const uint8_t MAX_PDP_FAILS_BEFORE_BACKOFF = 5;
const uint32_t PDP_BACKOFF_MS = 15000;
const uint32_t PDP_RECONNECT_TIMEOUT_MS = 30000;

bool ensurePdpAndNet() {
  String dummy;
  (void)sendAtSync("+CGDCONT=1,\"IP\",\"gigsky-02\"", dummy, 2000);

  if (!modem.isGprsConnected()) {
    // PROTECCIÓN CONTRA BLOQUEOS: Si hemos fallado muchas veces, esperar antes
    // de reintentar Esto evita bloqueos cuando se viaja entre redes celulares o
    // en zonas sin cobertura
    if (pdpReconnectFailCount >= MAX_PDP_FAILS_BEFORE_BACKOFF) {
      uint32_t timeSinceLastAttempt = millis() - lastPdpReconnectAttempt;
      if (timeSinceLastAttempt < PDP_BACKOFF_MS) {
        uint32_t remainingBackoff = PDP_BACKOFF_MS - timeSinceLastAttempt;
        Serial.printf(
            "[NET] Too many failures (%d), waiting %lu ms before retry\n",
            pdpReconnectFailCount, remainingBackoff);
        return false;
      } else {
        // Han pasado 15s, resetear contador y reintentar
        Serial.println("[NET] Backoff period over, resetting fail counter");
        pdpReconnectFailCount = 0;
      }
    }

    Serial.println("[NET] PDP down, reconnecting...");
    lastPdpReconnectAttempt = millis();

    // MINI-LOOP CON WATCHDOG RESET: modem.gprsConnect() puede bloquear 10-60s
    // Alimentamos el watchdog cada 1s para evitar reset del ESP32
    uint32_t reconStart = millis();
    bool reconOk = false;
    while (millis() - reconStart < PDP_RECONNECT_TIMEOUT_MS) {
      esp_task_wdt_reset(); // Evitar watchdog timeout cada 1s

      if (modem.gprsConnect(apn, gprsUser, gprsPass)) {
        reconOk = true;
        break;
      }

      delay(1000); // Esperar 1s entre intentos internos
    }

    if (!reconOk) {
      pdpReconnectFailCount++;
      Serial.printf(
          "[NET] PDP reconnect FAIL after %lu ms (fail count: %d/%d)\n",
          PDP_RECONNECT_TIMEOUT_MS, pdpReconnectFailCount,
          MAX_PDP_FAILS_BEFORE_BACKOFF);
      hasRed = false;
      return false;
    }

    // Éxito: resetear contador de fallos
    pdpReconnectFailCount = 0;
    hasRed = true;
    Serial.println("[NET] PDP reconnected OK");
  }

  String r;
  if (!sendAtSync("+NETOPEN?", r, 2000) || r.indexOf("+NETOPEN: 1") < 0) {
    if (!sendAtSync("+NETOPEN", r, 10000)) {
      Serial.println("[NET] NETOPEN FAIL");
      hasRed = false;
      return false;
    }
  }
  hasRed = true;
  return true;
}

// Helper: parsear +HTTPACTION: 0,200,123
void parseHttpActionResponse(const String &resp, int &code, int &dataLen) {
  code = -1;
  dataLen = -1;
  int p = resp.indexOf("+HTTPACTION:");
  int c1 = resp.indexOf(',', p);
  int c2 = resp.indexOf(',', c1 + 1);
  if (p >= 0 && c1 > 0 && c2 > c1) {
    code = resp.substring(c1 + 1, c2).toInt();
    dataLen = resp.substring(c2 + 1).toInt();
  }
}

// -------------------- HTTP GET (blocking, stable) --------------------
bool httpGet_webhook(const String &fullUrl) {
  Serial.printf("[HTTP][SYNC] URL length = %d\n", fullUrl.length());
  if (fullUrl.length() > 512) {
    Serial.println("[HTTP][WARN] URL >512 chars; SIM7600 +HTTPPARA may fail.");
  }

  if (!ensurePdpAndNet()) {
    logError("HTTP_PDP_FAIL", "ensurePdpAndNet", "PDP/NET setup failed");
    return false;
  }

  bool done = false, ok = false;
  (void)atTick(done, ok);

  (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
  if (!atRun("+HTTPINIT", "OK", "ERROR", 5000)) {
    Serial.println("[HTTP][ERR] HTTPINIT FAIL");
    logError("HTTP_INIT_FAIL", "HTTPINIT", at.resp);
    return false;
  }
  if (!atRun("+HTTPPARA=\"CID\",1", "OK", "ERROR", 2000)) {
    Serial.println("[HTTP][ERR] HTTPPARA CID FAIL");
    logError("HTTP_CID_FAIL", "HTTPPARA_CID", at.resp);
    (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
    return false;
  }
  {
    String cmd = "+HTTPPARA=\"URL\",\"" + fullUrl + "\"";
    if (!atRun(cmd, "OK", "ERROR", 6000)) {
      Serial.println("[HTTP][ERR] Set URL FAIL");
      logError("HTTP_URL_FAIL", "HTTPPARA_URL", at.resp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      return false;
    }
  }

  // Apagar LED para ahorrar energía durante transmisión
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  atBegin("+HTTPACTION=0", "+HTTPACTION:", "ERROR", 90000);
  int httpCode = -1, dataLen = -1;
  {
    bool actionDone = false, actionOk = false;
    uint32_t startTime = millis();
    // Timeout HTTP: 15s permite ~5 lecturas de PMS/GPS (3s cada una) antes de
    // abortar Ajustable para pruebas: reducir si red es muy rápida, aumentar si
    // red es muy lenta
    const uint32_t MAX_HTTP_WAIT_MS = 15000; // 15 segundos

    while (!actionDone) {
      esp_task_wdt_reset(); // Reset watchdog para evitar timeout durante HTTP

      if (atTick(actionDone, actionOk))
        break;

      // Check timeout
      if (millis() - startTime > MAX_HTTP_WAIT_MS) {
        uint32_t elapsed = millis() - startTime;
        Serial.printf("[HTTP][TIMEOUT] After %lu ms (bat=%.2fV)\n", elapsed,
                      batV);
        logError("HTTP_TIMEOUT", "HTTPACTION",
                 "Timeout=" + String(elapsed) + "ms bat=" + String(batV, 2) +
                     "V");
        (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
        updatePmLed((float)PM25);
        return false;
      }

      delay(1);
    }
    String actionResp = at.resp;
    if (!actionOk) {
      Serial.println("[HTTP][ERR] +HTTPACTION did not complete");
      logError("HTTP_ACTION_FAIL", "HTTPACTION", actionResp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      // Restaurar LED antes de salir
      updatePmLed((float)PM25);
      return false;
    }
    parseHttpActionResponse(actionResp, httpCode, dataLen);
    Serial.printf("[HTTP] code=%d len=%d\n", httpCode, dataLen);
    if (httpCode == -1) {
      Serial.println("[HTTP][ERR] Could not parse +HTTPACTION");
      logError("HTTP_PARSE_FAIL", "HTTPACTION", actionResp);
    }
  }

  if (dataLen > 0) {
    String cmd = "+HTTPREAD=0," + String(dataLen);
    String readResp;
    if (sendAtSync(cmd, readResp, 8000)) {
      Serial.println("[HTTP][READ] ----------------");
      Serial.println(readResp);
      Serial.println("[HTTP][READ] ----------------");
    } else {
      Serial.println("[HTTP][WARN] HTTPREAD failed");
    }
  }

  (void)atRun("+HTTPTERM", "OK", "ERROR", 2000);

  // Restaurar LED según nivel de PM2.5
  updatePmLed((float)PM25);

  return (httpCode >= 200 && httpCode < 300);
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\pms.ino"
// -------------------- PMS non-blocking parser (now with T/RH)
// --------------------
void readPMS() {
  // Ingest bytes
  while (pms.available() > 0) {
    int c = pms.read();
    if (c < 0)
      break;
    lastPmsSeen = millis();
    if (pmsHead < sizeof(pmsBuf))
      pmsBuf[pmsHead++] = (uint8_t)c;
    else {
      memmove(pmsBuf, pmsBuf + 1, sizeof(pmsBuf) - 1);
      pmsBuf[sizeof(pmsBuf) - 1] = (uint8_t)c;
    }
  }
  // Scan frames (32 bytes) with header 0x42 0x4D
  size_t i = 0;
  while (pmsHead >= 32 && i + 32 <= pmsHead) {
    uint8_t *frm = &pmsBuf[i];

    // Require header alignment to avoid false positives
    if (!(frm[0] == 0x42 && frm[1] == 0x4D)) {
      ++i;
      continue;
    }

    uint16_t cr1 = ((uint16_t)frm[30] << 8) | frm[31];
    uint16_t cr2 = 0;
    for (int k = 0; k < 30; ++k)
      cr2 += frm[k];
    if (cr1 == cr2) {
      // Standard concentration fields
      PM1 = ((uint16_t)frm[10] << 8) | frm[11];
      PM25 = ((uint16_t)frm[12] << 8) | frm[13];
      PM10 = ((uint16_t)frm[14] << 8) | frm[15];

      // Optional ST fields (0.1 units): bytes 24..27 (T,RH)
      uint16_t t10 = ((uint16_t)frm[24] << 8) | frm[25];
      uint16_t h10 = ((uint16_t)frm[26] << 8) | frm[27];
      float tC = t10 / 10.0f;
      float rH = h10 / 10.0f;
      if ((t10 != 0 || h10 != 0) && tC > -40 && tC < 85 && rH >= 0 &&
          rH <= 100) {
        pmsTempC = tC;
        pmsHum = rH;
      } else {
        pmsTempC = NAN;
        pmsHum = NAN;
      }

      size_t remain = pmsHead - (i + 32);
      memmove(pmsBuf, &frm[32], remain);
      pmsHead = remain;
      i = 0;
    } else {
      ++i;
    }
  }
  if (i > 0 && i < pmsHead) {
    memmove(pmsBuf, pmsBuf + i, pmsHead - i);
    pmsHead -= i;
  } else if (i >= pmsHead) {
    pmsHead = 0;
  }
}

// -------------------- PM2.5 smooth gradient (15/25/50) --------------------
static inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  float v = a + (b - a) * t;
  if (v < 0)
    v = 0;
  if (v > 255)
    v = 255;
  return (uint8_t)v;
}
void updatePmLed(float pm25) {
  uint8_t r = 0, g = 0, b = 0;
  if (pm25 <= 15.0f) {
    float t = pm25 / 15.0f;
    r = 0;
    g = lerp8(60, 127, t); // Mitad del brillo (120->60, 255->127)
    b = 0;
  } else if (pm25 <= 25.0f) {
    float t = (pm25 - 15.0f) / 10.0f;
    r = lerp8(0, 127, t); // Mitad del brillo (255->127)
    g = 127;              // Mitad del brillo (255->127)
    b = 0;
  } else if (pm25 <= 50.0f) {
    float t = (pm25 - 25.0f) / 25.0f;
    r = 127;              // Mitad del brillo (255->127)
    g = lerp8(127, 0, t); // Mitad del brillo (255->127)
    b = 0;
  } else {
    r = 127; // Mitad del brillo (255->127)
    g = 0;
    b = 0;
  }
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}
//-------------------------SDS198 non-blocking parser (usando Serial2
// UART2)-------------------------
// Función para leer una trama de datos del sensor.
bool readFrameSDS198(byte *buf) {
  // Sincroniza con la cabecera de la trama.
  int b;
  while ((b = Serial2.read()) != -1) {
    if ((byte)b == HEADER) {
      buf[0] = HEADER;
      break;
    }
  }
  if (b == -1)
    return false; // No se encontró la cabecera.

  // Lee los 9 bytes restantes de la trama.
  if (Serial2.readBytes(buf + 1, 9) != 9)
    return false;

  // Verifica el byte de comando y la cola de la trama.
  if (buf[1] != CMD || buf[9] != TAIL)
    return false;

  // Calcula el checksum sumando los bytes de datos (DATA1 a DATA6).
  byte sum = 0;
  for (int i = 2; i <= 7; i++) {
    sum += buf[i];
  }
  // Compara el checksum calculado con el recibido.
  return (sum == buf[8]);
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\rtc.ino"
// -------------------- Debug --------------------
bool syncRtcFromModem(); // Forward declaration
void printRtcOnce() {
  if (!rtcOK)
    return;
  DateTime now = rtc.now();
  Serial.printf("[RTC] %04d/%02d/%02d %02d:%02d:%02d epoch=%lu\n", now.year(),
                now.month(), now.day(), now.hour(), now.minute(), now.second(),
                (unsigned long)now.unixtime());
}
static uint8_t monthFromAbbrev(const char *m) {
  if (!m)
    return 1;
  if (!strncmp(m, "Jan", 3))
    return 1;
  if (!strncmp(m, "Feb", 3))
    return 2;
  if (!strncmp(m, "Mar", 3))
    return 3;
  if (!strncmp(m, "Apr", 3))
    return 4;
  if (!strncmp(m, "May", 3))
    return 5;
  if (!strncmp(m, "Jun", 3))
    return 6;
  if (!strncmp(m, "Jul", 3))
    return 7;
  if (!strncmp(m, "Aug", 3))
    return 8;
  if (!strncmp(m, "Sep", 3))
    return 9;
  if (!strncmp(m, "Oct", 3))
    return 10;
  if (!strncmp(m, "Nov", 3))
    return 11;
  if (!strncmp(m, "Dec", 3))
    return 12;
  return 1;
}

// Epoch de tiempo de compilación (__DATE__ y __TIME__)
static uint32_t compileUnixTime() {
  const char *d = __DATE__; // "Mmm dd yyyy"
  const char *t = __TIME__; // "HH:MM:SS"

  char mon[4] = {0};
  int day = 1, year = 2000, hh = 0, mm = 0, ss = 0;

  if (sscanf(d, "%3s %d %d", mon, &day, &year) != 3) {
    return MIN_VALID_EPOCH;
  }
  (void)sscanf(t, "%d:%d:%d", &hh, &mm, &ss);

  struct tm tmv = {};
  tmv.tm_year = year - 1900;
  tmv.tm_mon = monthFromAbbrev(mon) - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = hh;
  tmv.tm_min = mm;
  tmv.tm_sec = ss;
  return (uint32_t)mktime(&tmv);
}

// -------------------- Modem Time --------------------

static bool getModemEpoch(uint32_t &epoch_out) {
  String resp;
  if (!sendAtSync("+CCLK?", resp, 2000))
    return false;

  int q1 = resp.indexOf('"');
  int q2 = resp.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0)
    return false;

  String ts = resp.substring(q1 + 1, q2); // ej: "25/07/30,14:22:10+08"

  int s1 = ts.indexOf('/');
  int s2 = ts.indexOf('/', s1 + 1);
  int c = ts.indexOf(',');
  int k1 = ts.indexOf(':', c);
  int k2 = ts.indexOf(':', k1 + 1);
  if (s1 < 0 || s2 < 0 || c < 0 || k1 < 0 || k2 < 0)
    return false;

  int yy = ts.substring(0, s1).toInt() + 2000;
  int mo = ts.substring(s1 + 1, s2).toInt();
  int dd = ts.substring(s2 + 1, c).toInt();
  int hh = ts.substring(c + 1, k1).toInt();
  int mm = ts.substring(k1 + 1, k2).toInt();
  int ss = ts.substring(k2 + 1, k2 + 3).toInt();

  struct tm t = {};
  t.tm_year = yy - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = dd;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  time_t e = mktime(&t);
  if (e < MIN_VALID_EPOCH)
    return false;

  epoch_out = (uint32_t)e;
  return true;
}

// -------------------- Smart Sync --------------------
void syncRtcSmart() {
  if (!rtc.begin()) {
    Serial.println("[RTC] Not found");
    rtcOK = false;
    return;
  }
  rtcOK = true;

  // Cargar contador de syncs desde Preferences
  prefs.begin("rtc", false);
  rtcModemSyncCount = prefs.getUChar("syncCnt", 0);
  prefs.end();
  Serial.printf("[RTC] Modem sync count: %u/%u\n", rtcModemSyncCount,
                MAX_MODEM_SYNC_COUNT);

  // Obtener hora actual del RTC
  DateTime rtcNow = rtc.now();
  uint32_t rtcEpoch = rtcNow.unixtime();

  // Solo usar compile time si RTC perdió potencia o tiene hora inválida
  if (rtc.lostPower() || rtcEpoch < MIN_VALID_EPOCH) {
    uint32_t ctEpoch = compileUnixTime();
    rtc.adjust(DateTime(ctEpoch));
    Serial.println(
        "[RTC] Initialized with compile time (lost power or invalid)");
    Serial.printf("[RTC] Compile time: %lu\n", (unsigned long)ctEpoch);
  } else {
    Serial.printf(
        "[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d (epoch=%lu)\n",
        rtcNow.year(), rtcNow.month(), rtcNow.day(), rtcNow.hour(),
        rtcNow.minute(), rtcNow.second(), (unsigned long)rtcEpoch);
  }

  // Intentar sincronizar con modem (solo si no alcanzó límite)
  if (rtcModemSyncCount < MAX_MODEM_SYNC_COUNT) {
    syncRtcFromModem();
  } else {
    Serial.println("[RTC] Max modem sync count reached, skipping network sync");
    rtcNetSyncPending = false;
  }
}

// -------------------- Reset Modem Sync Counter --------------------
// Resetea el contador de sincronizaciones con el modem
// Útil si quieres forzar nuevas sincronizaciones después de alcanzar el límite
void resetModemSyncCounter() {
  rtcModemSyncCount = 0;
  prefs.begin("rtc", false);
  prefs.putUChar("syncCnt", 0);
  prefs.end();
  Serial.println("[RTC] Modem sync counter reset to 0");
}

// -------------------- Sync from Modem (llamable manualmente)
// -------------------- Esta función sincroniza el RTC con la hora del modem
// celular Puede ser llamada manualmente o desde syncRtcSmart() Retorna true si
// la sincronización fue exitosa
bool syncRtcFromModem() {
  if (!rtcOK) {
    Serial.println("[RTC] Cannot sync: RTC not available");
    return false;
  }

  // Habilitar actualización automática de zona horaria
  (void)atRun("+CTZU=1", "OK", "ERROR", 1000);
  (void)atRun("+CTZR=1", "OK", "ERROR", 1000);

  uint32_t modemEpoch = 0;
  bool haveModem = getModemEpoch(modemEpoch);

  if (!haveModem) {
    Serial.println("[RTC] Modem time not ready; will retry in loop");
    rtcNetSyncPending = true;
    rtcNextProbeMs = millis() + RTC_PROBE_PERIOD_MS;
    return false;
  }

  // Comparar con RTC actual
  uint32_t rtcEpoch = rtc.now().unixtime();
  long diff = (long)modemEpoch - (long)rtcEpoch;

  Serial.printf("[RTC] rtc=%lu modem=%lu diff=%ld s\n", (unsigned long)rtcEpoch,
                (unsigned long)modemEpoch, diff);

  // Solo sincronizar si la diferencia supera el umbral
  if (abs(diff) > RTC_SYNC_THRESHOLD) {
    rtc.adjust(DateTime(modemEpoch));
    Serial.println("[RTC] Synchronized to modem clock");

    // Incrementar y guardar contador
    rtcModemSyncCount++;
    prefs.begin("rtc", false);
    prefs.putUChar("syncCnt", rtcModemSyncCount);
    prefs.end();
    Serial.printf("[RTC] Sync count updated: %u/%u\n", rtcModemSyncCount,
                  MAX_MODEM_SYNC_COUNT);

    rtcNetSyncPending = false;
    return true;
  } else {
    Serial.println("[RTC] Within threshold; no sync needed");
    rtcNetSyncPending = false;
    return false;
  }
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\sd_card.ino"
// -------------------- SD helpers --------------------
#include "config.h"
extern SystemConfig config;
String generateCSVFileName() {
  // Genera nombre basado en DEVICE_ID_STR y fecha actual
  // Formato: HP02_DD_MM_YYYY.csv
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  char dateStr[16];
  snprintf(dateStr, sizeof(dateStr), "%02d_%02d_%04d", now.day(), now.month(),
           now.year());

  String name = "/HP" + String(DEVICE_ID_STR) + "_" + String(dateStr) + ".csv";
  Serial.print("[SD] CSV filename: ");
  Serial.println(name);
  return name;
}

void writeCSVHeader() {
  if (!SDOK)
    return;
  File f = SD.open(csvFileName, FILE_APPEND);
  if (f) {
    if (f.size() == 0) { // Only write header if file is empty
      f.println("ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,"
                "pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht31TempC,"
                "sht31Hum,resetReason,pm100");
      Serial.println("[SD] Wrote header to " + csvFileName);
    }
    f.close();
  } else {
    Serial.println("[SD][ERR] Failed to open " + csvFileName +
                   " to write header.");
  }
}

void writeErrorLogHeader() {
  if (!SDOK)
    return;
  if (SD.exists(logFilePath.c_str()))
    return; // Ya existe
  File f = SD.open(logFilePath.c_str(), FILE_WRITE);
  if (f) {
    f.println("timestamp,errorType,errorCode,rawResponse,operator,technology,"
              "signalQuality,registrationStatus,batteryV,uptime_s");
    f.close();
    Serial.println("[SD] Error log header created");
  }
}

// Variable global para almacenar la última línea guardada (para visualización
// en OLED)
extern String lastSavedCSVLine;

bool saveCSVData() {
  if (!SDOK || !loggingEnabled)
    return false;
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);

  // Detectar cambio de día (muy eficiente: 1 ciclo CPU)
  if (now.day() != lastDayLogged) {
    Serial.println("[SD] Day changed, creating new file...");
    csvFileName = generateCSVFileName();
    writeCSVHeader();
    lastDayLogged = now.day();

    // Persist new filename
    prefs.begin("system", false);
    prefs.putString("csvFile", csvFileName);
    prefs.end();
  }

  char hhmmss[9];
  snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d", now.hour(), now.minute(),
           now.second());
  String sht31Temp =
      (SHT31OK && !isnan(tempsht31)) ? String(tempsht31, 2) : "0";
  String sht31Humidity =
      (SHT31OK && !isnan(humsht31)) ? String(humsht31, 2) : "0";
  String line = String(millis()) + "," + hhmmss + "," + gpsDate + "," + gpsLat +
                "," + gpsLon + "," + gpsAlt + "," + gpsSpeedKmh + "," +
                String(PM1) + "," + String(PM25) + "," + String(PM10) + "," +
                (isnan(pmsTempC) ? "0" : String(pmsTempC, 1)) + "," +
                (isnan(pmsHum) ? "0" : String(pmsHum, 1)) + "," +
                String(rtcTempC, 2) + "," + String(batV, 2) + "," +
                String(csq) + "," + satellitesStr + "," + hdopStr + "," +
                (xtraLastOk ? "1" : "0") + "," + sht31Temp + "," +
                sht31Humidity + "," + rebootReason + "," + String(SDS198PM100);

  File f = SD.open(csvFileName, FILE_APPEND);
  if (f) {
    f.println(line);
    f.close();
    sdSaveCounter++;         // Incrementar contador de guardados exitosos en SD
    lastSavedCSVLine = line; // Guardar línea para visualización en OLED
    Serial.println(String("[SD] Saved line: ") + line);
    return true;
  } else {
    Serial.println("[SD][ERR] Failed to open " + csvFileName +
                   " for appending.");
    return false;
  }
}

// -------------------- Failed Transmission Log (Debug only)
// -------------------- Esta función guarda las transmisiones HTTP fallidas para
// análisis posterior NO se reintenta la transmisión automáticamente para evitar
// desfase de datos Variables globales ajustables para pruebas:
// - failedTxPath: Ruta del archivo CSV (definido en .ino principal)
void saveFailedTransmission(const String &url, const String &errorType) {
  if (!SDOK)
    return; // SD no disponible

  DateTime now = rtc.now();
  char timestamp[20];
  snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(),
           now.second());

  // Verificar si necesitamos crear el header (primera vez)
  bool needsHeader = !SD.exists(failedTxPath.c_str());

  File f = SD.open(failedTxPath.c_str(), FILE_APPEND);
  if (f) {
    // Escribir header si es la primera línea
    if (needsHeader) {
      f.println("timestamp,error_type,url");
      Serial.println("[FAILED_TX] Created header in " + failedTxPath);
    }

    // Escribir línea de fallo
    // Formato: timestamp,error_type,url (URL entre comillas por si tiene comas)
    String line = String(timestamp) + "," + errorType + ",\"" + url + "\"";
    f.println(line);
    f.close();

    Serial.println("[FAILED_TX] Logged: " + errorType);
  } else {
    Serial.println("[FAILED_TX][ERR] Could not open " + failedTxPath);
  }
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\serial_commands.ino"
// -------------------- Serial Commands --------------------
// Sistema de comandos seriales para debug y mantenimiento
// Uso: Enviar comando por serial monitor (115200 baud, newline)
// Ejemplo: "help" → muestra lista de comandos

#include "config.h"

extern SystemConfig config;
extern TinyGsm modem;
extern RTC_DS3231 rtc;
extern Preferences prefs;
extern bool rtcOK;
extern bool SDOK;
extern bool loggingEnabled;
extern bool streaming;
extern uint32_t sendCounter;
extern uint32_t sdSaveCounter;
extern String csvFileName;
extern String rebootReason;
extern String networkOperator;
extern String networkTech;
extern String signalQuality;
extern String registrationStatus;
extern float batV;
extern int csq;
extern String gpsStatus;
extern String satellitesStr;
extern String hdopStr;
extern const char *DEVICE_ID_STR;
extern String VERSION;
extern String logFilePath;
extern String failedTxPath;

// Forward declarations
void saveConfig();
void configSetDefaults();
void printConfig();
void applyLEDConfig();
void clearSDCard();
void printSDInfo();
void printSDFileList();
String generateCSVFileName();
void writeCSVHeader();
bool syncRtcFromModem();      // Assuming implemented or needed
void resetModemSyncCounter(); // Assuming implemented or needed
bool getModemEpoch(uint32_t &epoch);

void processSerialCommand() {
  if (!Serial.available())
    return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0)
    return;

  Serial.println("\n>>> Command: " + cmd);

  // -------------------- HELP --------------------
  if (cmd == "help" || cmd == "?") {
    Serial.println(F("=== HIRI PRO Serial Commands ==="));
    Serial.println(F("\n[RTC/Time]"));
    Serial.println(F("  rtc         - Show current RTC time"));
    // Serial.println(F("  rtcsync     - Force sync RTC with modem")); //
    // Removed as dependencies might be complex to port immediately
    // Serial.println(F("  rtcreset    - Reset modem sync counter"));
    // Serial.println(F("  modemtime   - Show modem time only"));

    Serial.println(F("\n[Counters]"));
    Serial.println(F("  counters    - Show SD/HTTP counters"));
    Serial.println(F("  resetcnt    - Reset both counters to 0"));
    Serial.println(F("  stats       - Show detailed statistics"));

    Serial.println(F("\n[SD Card]"));
    Serial.println(F("  sdinfo      - Show SD card info"));
    Serial.println(F("  sdlist      - List files on SD"));
    Serial.println(F("  sdnew       - Create new CSV file"));
    Serial.println(
        F("  sdclear     - Delete all files on SD (WARNING: irreversible!)"));

    Serial.println(F("\n[Network/Modem]"));
    Serial.println(F("  netinfo     - Show network info"));
    Serial.println(F("  csq         - Show signal quality"));

    Serial.println(F("\n[System]"));
    Serial.println(F("  sysinfo     - Show system info"));
    Serial.println(F("  reboot      - Reboot ESP32"));
    Serial.println(F("  mem         - Show memory usage"));

    Serial.println(F("\n[Streaming]"));
    Serial.println(F("  start       - Start streaming"));
    Serial.println(F("  stop        - Stop streaming"));

    Serial.println(F("\n[Configuration]"));
    Serial.println(F("  config              - Show all configuration"));
    Serial.println(F("  config sd/http/display/power - Show specific config"));
    Serial.println(F("  set sdauto on/off   - Mount SD on boot"));
    Serial.println(F("  set sdsave 3/60/600/1200 - SD save period (seconds)"));
    Serial.println(
        F("  set httpsend 3/60/600/1200 - HTTP send period (seconds)"));
    Serial.println(F("  set httptimeout 5-30 - HTTP timeout (seconds)"));
    Serial.println(F("  set oledoff on/off  - OLED auto-off"));
    Serial.println(F("  set oledtime 60/120/180 - OLED timeout (seconds)"));
    Serial.println(F("  set led on/off      - Enable NeoPixel"));
    Serial.println(F("  set ledbright 10/25/50/100 - LED brightness (%)"));
    Serial.println(F("  set autostart on/off - Autostart streaming on boot"));
    Serial.println(F("  set autowaitgps on/off - Wait GPS fix before start"));
    Serial.println(
        F("  set autogpsto 60-900 - GPS timeout (60s-15min, default: 600s)"));
    Serial.println(
        F("  set gnssmode 1/3/5/7/15 - GNSS mode (1=GPS, 3=GPS+GLO, 15=ALL)"));
    Serial.println(F("  configreset         - Reset to defaults"));
    Serial.println(F("  configsave          - Save config to flash"));

    Serial.println(F("\n"));
  }

  // -------------------- RTC COMMANDS --------------------
  else if (cmd == "rtc") {
    if (!rtcOK) {
      Serial.println("[RTC] Not available");
      return;
    }
    DateTime now = rtc.now();
    Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(), now.hour(), now.minute(),
                  now.second());
    Serial.printf("[RTC] Epoch: %lu\n", (unsigned long)now.unixtime());
    Serial.printf("[RTC] Temperature: %.2f°C\n", rtc.getTemperature());
    // Serial.printf("[RTC] Modem syncs: %u/3\n", rtcModemSyncCount);
  }

  // -------------------- COUNTER COMMANDS --------------------
  else if (cmd == "counters") {
    Serial.println("=== Counters ===");
    Serial.printf("SD saves:     %lu\n", (unsigned long)sdSaveCounter);
    Serial.printf("HTTP success: %lu\n", (unsigned long)sendCounter);
    if (sdSaveCounter > 0) {
      float successRate = (float)sendCounter / (float)sdSaveCounter * 100.0f;
      Serial.printf("Success rate: %.1f%%\n", successRate);
    }
  }

  else if (cmd == "resetcnt") {
    Serial.println("[COUNTERS] Resetting to 0...");
    sendCounter = 0;
    sdSaveCounter = 0;
    prefs.begin("system", false);
    prefs.putUInt("sendCnt", 0);
    prefs.putUInt("sdCnt", 0);
    prefs.end();
    Serial.println("[COUNTERS] ✓ Reset complete");
  }

  else if (cmd == "stats") {
    Serial.println("=== System Statistics ===");
    Serial.printf("Version:      %s\n", VERSION.c_str());
    Serial.printf("Device ID:    %s\n", DEVICE_ID_STR);
    Serial.printf("Uptime:       %lu s\n", millis() / 1000);
    Serial.printf("Streaming:    %s\n", streaming ? "ON" : "OFF");
    Serial.printf("SD logging:   %s\n", loggingEnabled ? "ON" : "OFF");
    Serial.printf("SD saves:     %lu\n", (unsigned long)sdSaveCounter);
    Serial.printf("HTTP success: %lu\n", (unsigned long)sendCounter);
    if (sdSaveCounter > 0) {
      float successRate = (float)sendCounter / (float)sdSaveCounter * 100.0f;
      Serial.printf("Success rate: %.1f%%\n", successRate);
    }
    Serial.printf("Battery:      %.2fV\n", batV);
    Serial.printf("CSQ:          %d\n", csq);
    Serial.printf("GPS:          %s\n", gpsStatus.c_str());
    Serial.printf("Satellites:   %s\n", satellitesStr.c_str());
    Serial.printf("HDOP:         %s\n", hdopStr.c_str());
    Serial.printf("Reboot reason:%s\n", rebootReason.c_str());
  }

  // -------------------- SD COMMANDS --------------------
  else if (cmd == "sdinfo") {
    printSDInfo();
  }

  else if (cmd == "sdlist") {
    printSDFileList();
  }

  else if (cmd == "sdnew") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    Serial.println("[SD] Creating new CSV file...");
    csvFileName = generateCSVFileName();
    writeCSVHeader();
    Serial.printf("[SD] ✓ New file created: %s\n", csvFileName.c_str());
    // Persist new filename
    prefs.begin("system", false);
    prefs.putString("csvFile", csvFileName);
    prefs.end();
  }

  else if (cmd == "sdclear") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    Serial.println(
        "[SD] ⚠ WARNING: This will delete ALL files on the SD card!");
    Serial.println("[SD] Type 'sdclear confirm' to proceed");
  }

  else if (cmd == "sdclear confirm") {
    clearSDCard();
  }

  // -------------------- NETWORK COMMANDS --------------------
  else if (cmd == "netinfo") {
    Serial.println("=== Network Info ===");
    Serial.printf("Operator:     %s\n", networkOperator.c_str());
    Serial.printf("Technology:   %s\n", networkTech.c_str());
    Serial.printf("CSQ:          %d\n", csq);
    Serial.printf("Registration: %s\n", registrationStatus.c_str());
    Serial.printf("PDP connected:%s\n", modem.isGprsConnected() ? "YES" : "NO");
    Serial.printf("Network conn: %s\n",
                  modem.isNetworkConnected() ? "YES" : "NO");
  }

  else if (cmd == "csq") {
    int newCsq = modem.getSignalQuality();
    Serial.printf("[MODEM] Signal Quality: %d", newCsq);
    if (newCsq == 0)
      Serial.println(" (no signal)");
    else if (newCsq < 10)
      Serial.println(" (marginal)");
    else if (newCsq < 15)
      Serial.println(" (ok)");
    else if (newCsq < 20)
      Serial.println(" (good)");
    else
      Serial.println(" (excellent)");
  }

  // -------------------- SYSTEM COMMANDS --------------------
  else if (cmd == "sysinfo") {
    Serial.println("=== System Info ===");
    Serial.printf("Firmware:     %s\n", VERSION.c_str());
    Serial.printf("Device ID:    %s\n", DEVICE_ID_STR);
    Serial.printf("Chip model:   %s\n", ESP.getChipModel());
    Serial.printf("Chip cores:   %d\n", ESP.getChipCores());
    Serial.printf("CPU freq:     %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash size:   %lu MB\n",
                  ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Free heap:    %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Uptime:       %lu s\n", millis() / 1000);
    Serial.printf("Reboot reason:%s\n", rebootReason.c_str());
  }

  else if (cmd == "mem") {
    Serial.println("=== Memory Usage ===");
    Serial.printf("Free heap:    %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Heap size:    %lu bytes\n", ESP.getHeapSize());
    Serial.printf("Min free heap:%lu bytes\n", ESP.getMinFreeHeap());
    Serial.printf("Max alloc:    %lu bytes\n", ESP.getMaxAllocHeap());
  }

  else if (cmd == "reboot") {
    Serial.println("[SYSTEM] Rebooting in 2 seconds...");
    delay(2000);
    ESP.restart();
  }

  // -------------------- STREAMING COMMANDS --------------------
  else if (cmd == "start") {
    if (streaming) {
      Serial.println("[STREAM] Already running");
    } else {
      Serial.println("[STREAM] Starting via serial command...");
      streaming = true;
      loggingEnabled = SDOK;
      Serial.printf("[STREAM] ✓ Started (SD logging: %s)\n",
                    loggingEnabled ? "ON" : "OFF");
    }
  }

  else if (cmd == "stop") {
    if (!streaming) {
      Serial.println("[STREAM] Already stopped");
    } else {
      Serial.println("[STREAM] Stopping...");
      streaming = false;
      loggingEnabled = false;
      Serial.println("[STREAM] ✓ Stopped");
    }
  }

  // -------------------- CONFIGURATION COMMANDS --------------------
  else if (cmd == "config") {
    printConfig();
  }

  else if (cmd == "config sd") {
    Serial.println("=== SD Configuration ===");
    Serial.printf("Auto-mount on boot: %s\n",
                  config.sdAutoMount ? "ON" : "OFF");
    Serial.printf("Save period:        %lu ms (%lu s)\n", config.sdSavePeriod,
                  config.sdSavePeriod / 1000);
  }

  else if (cmd == "config http") {
    Serial.println("=== HTTP Configuration ===");
    Serial.printf("Send period: %lu ms (%lu s)\n", config.httpSendPeriod,
                  config.httpSendPeriod / 1000);
    Serial.printf("Timeout:     %u seconds\n", config.httpTimeout);
  }

  else if (cmd == "config display") {
    Serial.println("=== Display Configuration ===");
    Serial.printf("Auto-off: %s\n", config.oledAutoOff ? "ON" : "OFF");
    Serial.printf("Timeout:  %lu ms (%lu s)\n", config.oledTimeout,
                  config.oledTimeout / 1000);
  }

  else if (cmd == "config power") {
    Serial.println("=== Power/LED Configuration ===");
    Serial.printf("NeoPixel enabled: %s\n", config.ledEnabled ? "YES" : "NO");
    Serial.printf("Brightness:       %u%%\n", config.ledBrightness);
  }

  else if (cmd.startsWith("set ")) {
    String param = cmd.substring(4);
    param.trim();

    // SD auto-mount
    if (param == "sdauto on") {
      config.sdAutoMount = true;
      saveConfig();
      Serial.println("[CONFIG] SD auto-mount: ON (will mount on next boot)");
    } else if (param == "sdauto off") {
      config.sdAutoMount = false;
      saveConfig();
      Serial.println("[CONFIG] SD auto-mount: OFF");
    }

    // SD save period
    else if (param == "sdsave 3") {
      config.sdSavePeriod = 3000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 3 seconds");
    } else if (param == "sdsave 60") {
      config.sdSavePeriod = 60000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 60 seconds");
    } else if (param == "sdsave 600") {
      config.sdSavePeriod = 600000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 600 seconds (10 min)");
    } else if (param == "sdsave 1200") {
      config.sdSavePeriod = 1200000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 1200 seconds (20 min)");
    }

    // HTTP send period
    else if (param == "httpsend 3") {
      config.httpSendPeriod = 3000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 3 seconds");
    } else if (param == "httpsend 60") {
      config.httpSendPeriod = 60000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 60 seconds");
    } else if (param == "httpsend 600") {
      config.httpSendPeriod = 600000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 600 seconds (10 min)");
    } else if (param == "httpsend 1200") {
      config.httpSendPeriod = 1200000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 1200 seconds (20 min)");
    }

    // HTTP timeout
    else if (param.startsWith("httptimeout ")) {
      int timeout = param.substring(12).toInt();
      if (timeout >= 5 && timeout <= 30) {
        config.httpTimeout = timeout;
        saveConfig();
        Serial.printf("[CONFIG] HTTP timeout: %d seconds\n", timeout);
      } else {
        Serial.println("[CONFIG] ✗ Timeout must be 5-30 seconds");
      }
    }

    // OLED auto-off
    else if (param == "oledoff on") {
      config.oledAutoOff = true;
      saveConfig();
      Serial.println("[CONFIG] OLED auto-off: ON");
    } else if (param == "oledoff off") {
      config.oledAutoOff = false;
      saveConfig();
      // Enable power
      // u8g2.setPowerSave(0); // Extern not available here directly, will
      // handle loop
      Serial.println("[CONFIG] OLED auto-off: OFF (always on)");
    }

    // OLED timeout
    else if (param == "oledtime 60") {
      config.oledTimeout = 60000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 60 seconds");
    } else if (param == "oledtime 120") {
      config.oledTimeout = 120000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 120 seconds (2 min)");
    } else if (param == "oledtime 180") {
      config.oledTimeout = 180000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 180 seconds (3 min)");
    }

    // LED enable/disable
    else if (param == "led on") {
      config.ledEnabled = true;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] NeoPixel: ON");
    } else if (param == "led off") {
      config.ledEnabled = false;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] NeoPixel: OFF");
    }

    // LED brightness
    else if (param == "ledbright 10") {
      config.ledBrightness = 10;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 10%");
    } else if (param == "ledbright 25") {
      config.ledBrightness = 25;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 25%");
    } else if (param == "ledbright 50") {
      config.ledBrightness = 50;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 50%");
    } else if (param == "ledbright 100") {
      config.ledBrightness = 100;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 100%");
    }

    // Autostart
    else if (param == "autostart on") {
      config.autostart = true;
      saveConfig();
      Serial.println("[CONFIG] Autostart: ON (will start on next boot)");
    } else if (param == "autostart off") {
      config.autostart = false;
      saveConfig();
      Serial.println("[CONFIG] Autostart: OFF");
    }

    // Autostart wait GPS
    else if (param == "autowaitgps on") {
      config.autostartWaitGps = true;
      saveConfig();
      Serial.println("[CONFIG] Autostart wait GPS: ON (will wait for fix)");
    } else if (param == "autowaitgps off") {
      config.autostartWaitGps = false;
      saveConfig();
      Serial.println("[CONFIG] Autostart wait GPS: OFF");
    }

    // Autostart GPS timeout
    else if (param.startsWith("autogpsto ")) {
      int val = param.substring(10).toInt();
      if (val >= 60 && val <= 900) {
        config.autostartGpsTimeout = val;
        saveConfig();
        Serial.printf("[CONFIG] Autostart GPS timeout: %u seconds (%u min)\n",
                      val, val / 60);
      } else {
        Serial.println(
            "[CONFIG] ✗ Invalid timeout (must be 60-900 seconds / 1-15 min)");
      }
    }

    // GNSS mode
    else if (param.startsWith("gnssmode ")) {
      int val = param.substring(9).toInt();
      if (val == 1 || val == 3 || val == 5 || val == 7 || val == 15) {
        config.gnssMode = val;
        saveConfig();
        Serial.print("[CONFIG] GNSS mode: ");
        Serial.print(val);
        Serial.println(" (Requires Reboot)");
      } else {
        Serial.println("[CONFIG] ✗ Invalid mode (valid: 1, 3, 5, 7, 15)");
      }
    }

    else {
      Serial.println("[CONFIG] ✗ Unknown parameter. Type 'help' for list.");
    }
  }

  else if (cmd == "configreset") {
    Serial.println("[CONFIG] Resetting to defaults...");
    configSetDefaults();
    saveConfig();
    applyLEDConfig();
    Serial.println("[CONFIG] ✓ Reset complete. Reboot to apply all changes.");
  }

  else if (cmd == "configsave") {
    saveConfig();
  }

  // -------------------- UNKNOWN COMMAND --------------------
  else {
    Serial.println("[CMD] Unknown command. Type 'help' for list.");
  }

  Serial.println();
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\ui.ino"
// -------------------- UI & Display Logic --------------------
// Integrates functionality from HIRI_PR0_MENU with GPSDebug backend data
#include <OneButton.h>
#include <U8g2lib.h>

// -------------------- External Variables (from FirmwarePro.ino)
// --------------------
// -------------------- UI --------------------
#include "config.h"
#include <Adafruit_NeoPixel.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <TinyGsmClient.h>
#include <U8g2lib.h>

extern SPIClass spiSD;
extern Preferences prefs;
extern const int SD_CS;
extern const int SD_SCLK;
extern const int SD_MISO;
extern const int SD_MOSI;

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RTC_DS3231 rtc;
extern TinyGsm modem;
extern bool rtcOK;
extern bool SDOK;
extern bool loggingEnabled;
extern bool streaming;
extern bool haveFix;
extern float batV;
extern uint16_t PM25;
extern float pmsTempC;
extern float pmsHum;
extern String satellitesStr;
extern struct AtSession at;
#include "config.h"
extern SystemConfig config;
extern volatile enum DisplayState displayState;
extern volatile uint32_t displayStateStartTime;
extern uint32_t lastOledActivity;
extern String csvFileName;
extern void writeCSVHeader();
extern void writeErrorLogHeader();
extern String generateCSVFileName();

#define SD_SAVE_DISPLAY_MS 2000

// --- Menu Structure ---
// Struct Menu
struct Menu {
  const char **items;
  const uint16_t *icons;
  uint8_t count;
};

// -------------------- Bitmap Icons --------------------
// Satellite icon 8x8, 1 bit/pixel, LSB first
static const unsigned char PROGMEM satelit_bitmap[8] = {0x06, 0x6E, 0x74, 0x38,
                                                        0x58, 0xE5, 0xC1, 0x07};

// Menú Principal
const char *topItems[] = {
    "PM2.5", "Temperatura", "Humedad", "Informacion",
    "OPCIONES"}; // Replaced "Empezar Muestreo" with Info/Options
const uint16_t topIcons[] = {
    0,      // null (PM2.5 value shown)
    0,      // null (Temp value shown)
    0,      // null (Hum value shown)
    0x0185, // ℹ️ Informacion
    0x0192  // ⚙️ Opciones
};

// Submenu: Opciones
const char *SubItems[] = {"Mensajes", "Configuracion", "Volver"};
const uint16_t SubIcons[] = {
    0x00EC, // ✉️ Mensajes
    0x015b, // ⚙️ Configuración
    0x01A9  // ←  Volver
};

// Menú de “Mensajes”
const char *msgItems[] = {"Camion", "Humo", "Construccion", "Volver"};
const uint16_t msgIcons[] = {
    0x2A1, // 🚚 Camión
    0x26C, // 💨 Humo
    0x09E, // 🏗️ Construcción
    0x01A9 // ←   Volver
};

// Menú de “Configuración”
const char *cfgItems[] = {"REDES", "WIFI AP", "Reiniciar", "Volver"};
const uint16_t cfgIcons[] = {
    0x01CC, // redes
    0x01F0, // WiFi
    0x00D5, // ↻ Reiniciar
    0x01A9  // ← Volver
};

// Menú de “Información” (Dynamic, accessed via main menu or specific item)
// Note: We might display this dynamically without a sub-menu struct if it's
// just values.

Menu menus[] = {
    {topItems, topIcons, sizeof(topItems) / sizeof(topItems[0])}, // 0: Main
    {SubItems, SubIcons, sizeof(SubItems) / sizeof(SubItems[0])}, // 1: Opciones
    {msgItems, msgIcons, sizeof(msgItems) / sizeof(msgItems[0])}, // 2: Mensajes
    {cfgItems, cfgIcons,
     sizeof(cfgItems) / sizeof(cfgItems[0])} // 3: Configuración
};

uint8_t menuDepth = 0; // 0 = principal, 1+ = submenus
uint8_t menuIndex = 0; // Índice seleccionado

// --- Button Instances (defined in main but used here) ---
// Declared extern in main helpers if needed, but we can access valid objects if
// they are global. We will define specific handler functions here that main
// will attach.

// --- UI Helper Functions ---
String getClockTime() {
  if (!rtcOK)
    return "??:??:??";
  DateTime now = rtc.now();
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.hour(), now.minute(),
           now.second());
  return String(buf);
}

int calcBatteryPercent(float v) {
  if (v >= 4.1)
    return 100; // 4.2V = 100%
  if (v <= 3.3)
    return 0;                          // 3.4V = 0% (límite operacional ESP32)
  return (int)((v - 3.4) / 0.8 * 100); // Rango: 3.4V-4.2V = 0.8V
}

void drawBatteryDynamic(int xPos, int yPos, float v) {
  // Validar voltaje para evitar valores inválidos
  if (isnan(v) || v < 0 || v > 5.0)
    v = 3.4;

  int pct = calcBatteryPercent(v);
  // Limitar porcentaje entre 0-100
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;

  float frac = pct / 100.0;
  const uint8_t w = 9, h = 6, tip = 2;
  // Posición ajustable
  uint8_t x = xPos;
  uint8_t y = yPos;
  // Contorno y terminal
  u8g2.drawFrame(x, y, w, h);
  u8g2.drawBox(x + w, y + 2, tip, h - 4);

  // Nivel interno o icono de carga crítica
  if (pct == 0) {
    // si es 0 ponemos la C
    u8g2.setFont(u8g2_font_5x7_tf);
    char s = 'C';
    u8g2.setCursor(x - 6, y + h);
    u8g2.print(s);

    u8g2.drawLine(x + 4, y + 1, x + 2, y + 3);
    u8g2.drawLine(x + 2, y + 3, x + 5, y + 3);
    u8g2.drawLine(x + 5, y + 3, x + 3, y + 5);
  } else {
    // Calcular ancho del relleno y limitar al tamaño del marco
    uint8_t fillWidth = (uint8_t)((w - 2) * frac);
    if (fillWidth > (w - 2))
      fillWidth = (w - 2);
    if (fillWidth > 0) {
      u8g2.drawBox(x + 1, y + 1, fillWidth, h - 2);
    }
  }
}

void drawHeader() {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 9, getClockTime().c_str());

  // Satellite icon (custom bitmap)
  if (haveFix) {
    u8g2.drawXBMP(65, 1, 8, 8, satelit_bitmap); // Bitmap del satélite
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(73, 9);     // esto rompe con la nueva grafica del footer
    u8g2.print(satellitesStr); //
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(65, 9, 0x0118); // Error icon
  }

  // WiFi/Signal icon
  bool networkError = (csq == 99);
  if (networkError) {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(90, 9, 0x0118); // Error icon at signal location
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(90, 9, 0x00FD); // wifi
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(98, 9);
    u8g2.print(csq);
  }

  drawBatteryDynamic(115, 3, batV);
}

void drawFooterCircles(uint8_t cnt, uint8_t sel) {
  const uint8_t dia = 4, sp = 8;
  uint8_t totalW = cnt * dia + (cnt - 1) * sp;
  int16_t sx = (128 - totalW) / 2;
  for (uint8_t i = 0; i < cnt; i++) {
    uint8_t x = sx + i * (dia + sp);
    if (i == sel)
      u8g2.drawDisc(x + dia / 2, 59, dia / 2);
    else
      u8g2.drawCircle(x + dia / 2, 59, dia / 2);
  }
}

void drawSensorValue(uint8_t idx) {
  char buf[24];
  String baseF = "";
  if (idx == 0) { // PM2.5
    snprintf(buf, sizeof(buf), "%u", PM25);
    baseF = "PM 2.5 (ug/m3)";
  } else if (idx == 1) { // Temp
    if (isnan(pmsTempC))
      snprintf(buf, sizeof(buf), "--.-");
    else
      dtostrf(pmsTempC, 0, 1, buf);
    baseF = "Temperatura (C)";
  } else if (idx == 2) { // Hum
    if (isnan(pmsHum))
      snprintf(buf, sizeof(buf), "--.-");
    else
      dtostrf(pmsHum, 0, 1, buf);
    baseF = "Humedad (%)";
  }

  u8g2.setFont(u8g2_font_logisoso24_tn);
  int w = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - w) / 2, 43, buf);

  u8g2.setFont(u8g2_font_5x7_tf);
  int wLbl = u8g2.getStrWidth(baseF.c_str());
  u8g2.drawStr((128 - wLbl) / 2, 53, baseF.c_str());
}

void drawMenuItemWithIcon(uint8_t depth, uint8_t idx) {
  const char *txt = menus[depth].items[idx];
  const uint16_t *ic = menus[depth].icons;

  // 0) Definir posición de la línea de texto
  u8g2.setFont(u8g2_font_6x12_tf);
  const uint8_t yText = 53;
  uint8_t textH = u8g2.getMaxCharHeight();

  // 1) Dibuja icono centrado y encima del texto
  if (ic && ic[idx] != 0) {
    u8g2.setFont(u8g2_font_streamline_all_t);
    // Adjust font if icon not found or use specific font per icon range if
    // needed Assuming icons are from the same font set or mapped correctly If
    // icons are from open_iconic, we need to switch fonts. The previous code
    // used a mix. Let's standarize or check ranges. For simplicity, using
    // u8g2_font_open_iconic_all_2x_t for generic icons
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);

    uint8_t iconW = u8g2.getMaxCharWidth();
    // Centrar horizontalmente
    uint8_t xIcon = (128 - iconW) / 2;
    // Colocar icono
    u8g2.drawGlyph(xIcon, 35, ic[idx]);
  }

  // 2) Dibuja texto centrado horizontalmente en yText
  u8g2.setFont(u8g2_font_6x12_tf);
  int tw = u8g2.getStrWidth(txt);
  uint8_t xText = (128 - tw) / 2;
  u8g2.drawStr(xText, yText, txt);
}

void renderDisplay() {
  u8g2.clearBuffer();
  drawHeader();

  // Special States (e.g. SD Saved confirmation)
  if (displayState == DISP_SD_SAVED) {
    if (millis() - displayStateStartTime < SD_SAVE_DISPLAY_MS) {
      u8g2.setFont(u8g2_font_6x12_tf);
      u8g2.drawStr(20, 35, "DATOS GUARDADOS");
      u8g2.sendBuffer();
      return;
    } else {
      displayState = DISP_NORMAL;
    }
  }

  // Normal Menu Rendering
  if (menuDepth == 0) {
    // Menu Principal
    // Items 0-2 son sensores (PM2.5, Temp, Hum)
    if (menuIndex < 3) {
      drawSensorValue(menuIndex);
      // Also show small GPS info if available
      if (menuIndex == 0 && haveFix) {
        // esto rompe la integracion del footer  original podria mostrar cuando
        // esta guardando en la sesion
        // u8g2.setFont(u8g2_font_5x7_tf);
        // u8g2.setCursor(0, 64);
        // u8g2.print("Sats:" + satellitesStr);
      }
    } else {
      // Items 3+ (Infos, Opciones)
      drawMenuItemWithIcon(menuDepth, menuIndex);
    }
  } else {
    // Submenus
    drawMenuItemWithIcon(menuDepth, menuIndex);
  }

  drawFooterCircles(menus[menuDepth].count, menuIndex);
  u8g2.sendBuffer();
}

// --- Action Handlers ---

void handleRestart() {
  u8g2.clearBuffer();
  u8g2.drawStr(30, 30, "REINICIANDO...");
  u8g2.sendBuffer();
  delay(1000);
  ESP.restart();
}

void handleConfigWifi() {
  // Toggle WiFi AP
  if (!wifiModeActive) {
    startWifiApServer(); // This will take over display
  } else {
    stopWifiApServer();
  }
}

// --- Interaction Logic ---

// BTN1 Click: Next Option
void ui_btn1_click() {
  menuIndex = (menuIndex + 1) % menus[menuDepth].count;
  lastOledActivity = millis();
  if (config.oledAutoOff)
    u8g2.setPowerSave(0);
}

// BTN2 Click: Select / Enter
void ui_btn2_click() {
  lastOledActivity = millis();
  if (config.oledAutoOff)
    u8g2.setPowerSave(0);

  if (menuDepth == 0) {
    // Main Menu
    if (menuIndex == 3) { // Information (Just show, maybe detailed view?)
                          // For now, do nothing or toggle detail
    } else if (menuIndex == 4) { // Opciones
      menuDepth = 1;
      menuIndex = 0;
    }
  } else if (menuDepth == 1) {
    // Opciones Menu
    if (menuIndex == 0) { // Mensajes
      menuDepth = 2;
      menuIndex = 0;
    } else if (menuIndex == 1) { // Configuracion
      menuDepth = 3;
      menuIndex = 0;
    } else if (menuIndex == 2) { // Volver
      menuDepth = 0;
      menuIndex = 0;
    }
  } else if (menuDepth == 3) {
    // Configuration Menu
    if (menuIndex == 1) { // WIFI AP
      handleConfigWifi();
    } else if (menuIndex == 2) { // Reiniciar
      handleRestart();
    } else if (menuIndex == 3) { // Volver
      menuDepth = 1;
      menuIndex = 0;
    }
  } else {
    // Generic Back for other menus
    if (String(menus[menuDepth].items[menuIndex]) == "Volver") {
      menuDepth--;
      menuIndex = 0;
    }
  }
}

// BTN2 Hold: Action / Back
void ui_btn2_hold() {
  lastOledActivity = millis();
  if (config.oledAutoOff)
    u8g2.setPowerSave(0);

  if (menuDepth == 0) {
    // CRITICAL: Main Screen Hold -> Toggle Sampling
    if (streaming) {
      // STOP
      streaming = false;
      loggingEnabled = false;
      Serial.println("[UI] User Request: STOP Streaming/Logging");
      prefs.begin("system", false);
      prefs.putBool("streaming", false);
      prefs.end();
      // Visual Feedback
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_open_iconic_all_4x_t);
      u8g2.drawGlyph(48, 48, 0x00F9); // Stop icon (square)
      u8g2.sendBuffer();
      delay(1000);
    } else {
      // START
      streaming = true;
      // Init SD if needed
      if (!SDOK) {
        spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
        SDOK = SD.begin(SD_CS, spiSD);
        loggingEnabled = SDOK;
        if (SDOK) {
          csvFileName = generateCSVFileName();
          writeCSVHeader();
          writeErrorLogHeader();
          prefs.begin("system", false);
          prefs.putString("csvFile", csvFileName);
          prefs.end();
        }
      } else {
        loggingEnabled = true;
      }

      Serial.println("[UI] User Request: START Streaming/Logging");
      prefs.begin("system", false);
      prefs.putBool("streaming", true);
      prefs.end();

      // Visual Feedback
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_open_iconic_all_4x_t);
      u8g2.drawGlyph(48, 48, 0x00E9); // Play icon
      u8g2.sendBuffer();
      delay(1000);
    }
  } else {
    // Submenus: Go Back
    menuDepth--;
    menuIndex = 0;
  }
}

void updateDisplayStateMachine() {
  // Nothing to update state-wise here, handled in renderDisplay and event
  // handlers
}

#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\wifi.ino"
///////////////////////////////////////////// WIFI
// ===== WiFi AP File Server (single definition) =====

// ---- HTML ----
const char *headerHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 SD Manager</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 800px; margin: auto; padding: 10px; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #f4f4f4; }
    button { margin: 2px; padding: 5px 10px; }
    input[type=file] { margin-top: 10px; }
    progress { width: 100%; margin-top: 5px; }
    #uploadStatus { display: inline-block; margin-left: 10px; }
    .toolbar { margin-top:10px; padding:10px; border:1px solid #ddd; background:#fafafa; }
    .error { color: red; font-weight: bold; }
    .loading { color: #666; font-style: italic; }
  </style>
  <script>
    function downloadFile(fname) {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', '/download?file=' + encodeURIComponent(fname), true);
      xhr.responseType = 'blob';
      xhr.onprogress = function(event) {
        var percent = event.lengthComputable ? Math.floor((event.loaded / event.total) * 100) : '';
        var el = document.getElementById('progress_' + fname);
        if (el) el.innerText = percent ? percent + '%' : '';
      };
      xhr.onload = function() {
        var url = window.URL.createObjectURL(xhr.response);
        var a = document.createElement('a');
        a.href = url;
        a.download = fname;
        a.click();
        window.URL.revokeObjectURL(url);
      };
      xhr.send();
    }
    function deleteFile(fname) {
      if (confirm('Delete ' + fname + '?')) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', '/delete?file=' + encodeURIComponent(fname), true);
        xhr.onload = function() { location.reload(); };
        xhr.send();
      }
    }
    function renameFile(fname) {
      var newname = prompt('Rename ' + fname + ' to:', fname);
      if (newname && newname !== fname) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', '/rename?file=' + encodeURIComponent(fname) + '&newname=' + encodeURIComponent(newname), true);
        xhr.onload = function() { location.reload(); };
        xhr.send();
      }
    }
    function uploadFile() {
      var fileInput = document.getElementById('fileInput');
      if (fileInput.files.length == 0) { alert('Select a file'); return; }
      var file = fileInput.files[0];
      var formData = new FormData();
      formData.append('upload', file);

      var xhr = new XMLHttpRequest();
      xhr.open('POST', '/upload', true);

      xhr.upload.onprogress = function(event) {
        var percent = event.lengthComputable ? Math.floor((event.loaded / event.total) * 100) : 0;
        document.getElementById('uploadProgress').value = percent;
        document.getElementById('uploadStatus').innerText = percent + '%';
      };
      xhr.onload = function() {
        document.getElementById('uploadStatus').innerText = (xhr.status === 200) ? 'Upload complete' : 'Upload failed';
        setTimeout(function() { location.reload(); }, 1000);
      };
      xhr.send(formData);
    }
    function deleteAll(){
      if (confirm('Delete ALL files from SD?')) {
        var xhr = new XMLHttpRequest();
        xhr.open('POST','/delete_all',true);
        xhr.onload = function(){ alert(xhr.responseText); location.reload(); };
        xhr.send();
      }
    }

    // Fetch con timeout y reintentos
    function fetchWithRetry(url, elementId, retries = 3, timeout = 5000) {
      var attempt = 0;
      function tryFetch() {
        var xhr = new XMLHttpRequest();
        var timeoutId;

        xhr.open('GET', url, true);
        xhr.timeout = timeout;

        xhr.onload = function() {
          clearTimeout(timeoutId);
          if (xhr.status === 200) {
            document.getElementById(elementId).innerText = xhr.responseText;
            document.getElementById(elementId).className = '';
          } else {
            retry();
          }
        };

        xhr.onerror = function() {
          clearTimeout(timeoutId);
          retry();
        };

        xhr.ontimeout = function() {
          clearTimeout(timeoutId);
          retry();
        };

        function retry() {
          attempt++;
          if (attempt < retries) {
            document.getElementById(elementId).innerText = 'Retry ' + attempt + '/' + retries + '...';
            setTimeout(tryFetch, 500);
          } else {
            document.getElementById(elementId).innerText = 'Failed';
            document.getElementById(elementId).className = 'error';
          }
        }

        xhr.send();
      }
      tryFetch();
    }
  </script>
</head>
<body>
  <h1>HIRI-PRO SD File Manager</h1>

  <div class="toolbar">
    <div><b>AP SSID:</b> <span id="ssid" class="loading">Loading...</span> &nbsp; <b>Password:</b> 12345678 &nbsp; <b>IP:</b> <span id="ip" class="loading">Loading...</span></div>
    <div style="margin-top:8px">
      <input type="file" id="fileInput">
      <button onclick="uploadFile()">Upload</button>
      <button onclick="deleteAll()">Delete ALL files</button>
      <progress id="uploadProgress" value="0" max="100"></progress>
      <span id="uploadStatus"></span>
    </div>
  </div>

  <table>
    <tr><th>Name</th><th>Size (bytes)</th><th>Actions</th><th>Progress</th></tr>
)rawliteral";

const char *footerHtml = R"rawliteral(
  </table>
  <script>
    // Cargar IP y SSID con reintentos automáticos
    window.onload = function() {
      fetchWithRetry('/ip', 'ip', 5, 3000);
      fetchWithRetry('/ssid', 'ssid', 5, 3000);
    };
  </script>
</body>
</html>
)rawliteral";

#include <ctype.h>

// Replace non-alphanumeric chars by '_'
static String sanitizeForId(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    out += (isalnum((unsigned char)c) ? c : '_');
  }
  return out;
}

// ---- Routes implementation ----
void listFiles() {
  // Enviar header primero
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(headerHtml);

  // Iterar archivos y enviar cada fila
  File root = SD.open("/");
  if (root) {
    File file = root.openNextFile();
    int fileCount = 0;
    while (file && fileCount < 100) { // Límite de seguridad
      if (!file.isDirectory()) {
        String name = file.name();
        size_t fsize = file.size();
        file.close();

        String row = "<tr>";
        row += "<td>" + name + "</td>";
        row += "<td>" + String(fsize) + "</td>";
        row += "<td>";
        row += "<button onclick=\"downloadFile('" + name +
               "')\">Download</button>";
        row += "<button onclick=\"deleteFile('" + name + "')\">Delete</button>";
        row += "<button onclick=\"renameFile('" + name + "')\">Rename</button>";
        row += "</td>";
        row += "<td><span id='progress_" + name + "'></span></td>";
        row += "</tr>\n";

        server.sendContent(row);
        fileCount++;
      } else {
        file.close();
      }
      file = root.openNextFile();
      yield(); // Dar tiempo al watchdog
    }
    root.close();
  }

  // Enviar footer
  server.sendContent(footerHtml);
  server.sendContent(""); // Finalizar chunked encoding
}

void handleFileDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "File not specified");
    return;
  }
  String filename = server.arg("file");
  File f = SD.open("/" + filename);
  if (!f) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Content-Length", String(f.size()));
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleFileDelete() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "File not specified");
    return;
  }
  String filename = server.arg("file");
  bool ok = SD.remove("/" + filename);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Delete failed");
}

void handleFileRename() {
  if (!server.hasArg("file") || !server.hasArg("newname")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  String oldname = server.arg("file");
  String newname = server.arg("newname");
  bool ok = SD.rename("/" + oldname, "/" + newname);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Rename failed");
}

extern File uploadFile;

void handleFileUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/"))
      filename = "/" + filename;
    uploadFile = SD.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile)
      uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile)
      uploadFile.close();
  }
}

void handleDeleteAll() {
  File root = SD.open("/");
  if (!root) {
    server.send(500, "text/plain", "SD open error");
    return;
  }
  int okCnt = 0, failCnt = 0;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      file.close(); // cerrar antes de borrar
      if (SD.remove("/" + name))
        okCnt++;
      else
        failCnt++;
      root.close(); // reiniciar iteración limpia
      root = SD.open("/");
      file = root.openNextFile();
      continue;
    }
    file = root.openNextFile();
  }
  root.close();
  server.send(200, "text/plain",
              "Deleted: " + String(okCnt) + ", Failed: " + String(failCnt));
}

void handleIp() { server.send(200, "text/plain", WiFi.softAPIP().toString()); }

void handleSsid() { server.send(200, "text/plain", AP_SSID_STR); }

void setupWifiRoutes() {
  server.on("/", HTTP_GET, listFiles);
  server.on("/download", HTTP_GET, handleFileDownload);
  server.on("/delete", HTTP_GET, handleFileDelete);
  server.on("/rename", HTTP_GET, handleFileRename);
  server.on(
      "/upload", HTTP_POST, []() { server.send(200, "text/plain", "OK"); },
      handleFileUpload);
  server.on("/delete_all", HTTP_POST, handleDeleteAll);
  server.on("/ip", HTTP_GET, handleIp);
  server.on("/ssid", HTTP_GET, handleSsid);
}

//-----------handrles start/stop wifi server
void startWifiApServer() {
  // Monta SD si no está
  if (!SDOK) {
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);
    Serial.println(SDOK ? "[SD] Ready" : "[SD] FAIL");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID_STR.c_str(), AP_PASSWORD);
  delay(100);

  setupWifiRoutes();
  server.begin();
  wifiModeActive = true;

  IPAddress ip = WiFi.softAPIP();
  apIpStr = ip.toString(); // actualizar string mostrado en OLED
  Serial.print("[WiFi] AP SSID: ");
  Serial.println(AP_SSID_STR);
  Serial.print("[WiFi] AP IP: ");
  Serial.println(ip);

  // OLED banner de Wi-Fi
  // Note: oledStatus might be in ui.ino or main. We need to make sure it's
  // accessible. In previous structure it was in main. I will assume it will be
  // available.
  oledStatus("WIFI MODE", "SSID: " + AP_SSID_STR,
             "PASS: " + String(AP_PASSWORD), "IP: " + ip.toString());
}

void stopWifiApServer() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiModeActive = false;
  Serial.println("[WiFi] AP stopped");
  // (opcional) refrescar OLED con tu UI normal al siguiente ciclo
}

