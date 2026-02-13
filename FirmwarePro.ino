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
String VERSION = "Pro V0.0.13";

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
// Separamos guardado SD y transmisión HTTP con timers independientes.
uint32_t lastHttpSend = 0;
uint32_t lastSdSave = 0;
// Estado de actividad para UI (header U/S).
uint32_t lastHttpActivityMs = 0;
uint32_t lastSdActivityMs = 0;
bool lastHttpOk = false;
bool lastSdOk = false;
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
bool sendCurrentMeasurement();

// UI Event Handlers
extern void ui_btn1_click();
extern void ui_btn2_click();
extern void ui_btn2_hold();

// Oled Status Helper (used by wifi/main)
// Renderiza un estado rápido en OLED con hasta 4 líneas de texto.
// Se usa para feedback de arranque, red, módem y operaciones críticas.
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
// Inicia una sesión AT no bloqueante, guarda expectativas y timeout.
// La respuesta se procesa luego con atTick() para no congelar el loop.
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

// Avanza la máquina de estados AT leyendo serial y detectando fin/timeout.
// También enruta tramas NMEA entrantes al parser GNSS cuando aparecen.
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

// Ejecuta un comando AT de forma bloqueante hasta éxito, error o timeout.
// Es un helper práctico para setup y tareas puntuales de configuración.
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

// Envía AT y devuelve la respuesta completa en un String para diagnóstico.
// Útil cuando se necesita parsear contenido (no solo OK/ERROR).
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

// Consulta operador, tecnología y registro de red desde el módem.
// Actualiza variables globales usadas en UI, logs y comandos seriales.
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

// -------------------- Telemetry Tx --------------------
// NOTA DE INTEGRACION:
// - "streaming" controla transmisión HTTP.
// - "loggingEnabled" controla guardado en SD.
// - Ambos están separados a propósito para evitar acoplar guardar/transmitir.
// Construye payload/URL de medición según hardware activo y envía por HTTP.
// Persiste contadores en flash y registra fallos en SD cuando corresponde.
bool sendCurrentMeasurement() {
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

  if (SHT31OK == true) {
    const String v18 = isnan(tempsht31) ? "0" : safeFloatStr(tempsht31);
    const String v19 = isnan(humsht31) ? "0" : safeFloatStr(humsht31);
    val = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 +
          "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," + v13 +
          "," + v14 + "," + v15 + "," + v16 + "," + v17 + "," + v18 + "," + v19;
    url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES +
          "&idsVariables=" + IDS_VARIABLESSHT31 + "&valores=" + val;
  } else if (String(DEVICE_ID_STR) == "06") {
    const String v18 = safeUIntStr(SDS198PM100);
    val = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 +
          "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," + v13 +
          "," + v14 + "," + v15 + "," + v16 + "," + v17 + "," + v18;
    url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES +
          "&idsVariables=" + IDS_VARIABLES06 + "&valores=" + val;
  } else {
    val = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 +
          "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," + v13 +
          "," + v14 + "," + v15 + "," + v16 + "," + v17;
    url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES +
          "&idsVariables=" + IDS_VARIABLES + "&valores=" + val;
  }

  Serial.println("[HTTP] GET " + url);
  if (httpGet_webhook(url)) {
    sendCounter++;
    prefs.begin("system", false);
    prefs.putUInt("sendCnt", sendCounter);
    prefs.putUInt("sdCnt", sdSaveCounter);
    prefs.end();
    Serial.println("[HTTP] OK");
    return true;
  }

  saveFailedTransmission(url, "HTTP_FAIL");
  Serial.println("[HTTP] FAIL");
  return false;
}

// -------------------- SETUP --------------------
// Inicializa hardware, configuración persistente y servicios base del firmware.
// Define estado de arranque seguro y prepara módem/GNSS/SD/UI para operación.
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
  wasStreamingBeforeBoot = prefs.getBool("streaming", false);
  prefs.end();

  // Estado runtime por defecto.
  streaming = false;
  loggingEnabled = false;

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
  // Ajuste de respuesta de botones para navegación de menús más ágil.
  btn1.setDebounceTicks(20);
  btn2.setDebounceTicks(20);
  btn2.setClickTicks(160);
  // Hold más largo para evitar activaciones accidentales durante navegación.
  btn2.setPressTicks(800);

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
  // Política actual:
  // - Verificar SD al inicio.
  // - Si antes estaba activo y el reinicio fue "solo" (no SW manual),
  //   reanudar streaming+logging.
  if (config.sdAutoMount || wasStreamingBeforeBoot) {
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);
    if (SDOK) {
      if (!csvFileName.length() || !SD.exists(csvFileName.c_str())) {
        csvFileName = generateCSVFileName();
        writeCSVHeader();
      }

      prefs.begin("system", false);
      prefs.putString("csvFile", csvFileName);
      prefs.end();

      // Autoresume SOLO en reinicios claramente inesperados por watchdog/panic.
      // Evita retomar streaming tras reinicios manuales, power-on o estados ambiguos.
      bool rebootWasUnexpected =
          (rebootReason == "Panic" || rebootReason == "IntWatchdog" ||
           rebootReason == "TaskWatchdog" || rebootReason == "OtherWatchdog");

      if (wasStreamingBeforeBoot && rebootWasUnexpected) {
        streaming = true;
        loggingEnabled = true;
        writeErrorLogHeader();
        Serial.println(
            "[BOOT] Auto-resume enabled (previous state + unexpected reboot)");
      } else {
        Serial.println("[BOOT] SD detected. Streaming/logging remain OFF");
      }
    }
  }

  Serial.println("[READY] Loop starting");
}

// -------------------- LOOP --------------------
bool FirstLoop = true;
// Bucle principal no bloqueante: sensores, UI, watchdog y scheduler de tareas.
// Ejecuta guardado SD y transmisión HTTP en timers separados por configuración.
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
  if (millis() - lastDisplayUpdate > 60) {
    lastDisplayUpdate = millis();
    renderDisplay();
  }

  // Auto Off
  if (config.oledAutoOff &&
      (millis() - lastOledActivity > config.oledTimeout)) {
    u8g2.setPowerSave(1);
  }

  // Guardado SD (separado de transmisión HTTP)
  // Nota: por diseño de esta etapa, loggingEnabled inicia en false.
  if (loggingEnabled && (millis() - lastSdSave >= config.sdSavePeriod)) {
    lastSdSave = millis();
    bool sdSaved = saveCSVData();
    lastSdActivityMs = millis();
    lastSdOk = sdSaved;
    if (sdSaved) {
      displayState = DISP_SD_SAVED;
      displayStateStartTime = millis();
      renderDisplay();
    }
  }

  // Transmisión HTTP (separada de guardado SD)
  if (streaming && (millis() - lastHttpSend >= config.httpSendPeriod)) {
    lastHttpSend = millis();
    bool txOk = sendCurrentMeasurement();
    lastHttpActivityMs = millis();
    lastHttpOk = txOk;
  }

  // Serial Commands
  processSerialCommand();
}
