// -------------------- UI & Display Logic --------------------
// Integrates functionality from HIRI_PR0_MENU with GPSDebug backend data
#include <OneButton.h>
#include <U8g2lib.h>
#include <string.h>

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
extern String gpsStatus;
extern float batV;
extern uint32_t lastHttpActivityMs;
extern uint32_t lastSdActivityMs;
extern bool lastHttpOk;
extern bool lastSdOk;
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
extern String VERSION;
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
const char *topItems[] = {"PM2.5", "Temperatura", "Humedad", "Empezar Muestreo",
                          "OPCIONES"};
const uint16_t topIcons[] = {
    0,      // null
    0,      // null
    0,      // null
    0x01A5, // muestreo
    0x0192  // opciones
};

// Submenu: Opciones
const char *SubItems[] = {"Mensajes", "Configuracion", "Informacion", "Volver"};
const uint16_t SubIcons[] = {
    0x00EC, // mensajes
    0x015b, // configuración
    0x0185, // información
    0x01A9  // volver
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
const char *cfgItems[] = {"REDES", "GUARDADO", "RTC", "Reiniciar", "Volver"};
const uint16_t cfgIcons[] = {
    0x01CC, // redes
    0x0176, // guardado
    0x01CB, // rtc/función
    0x00D5, // reiniciar
    0x01A9  // volver
};

// Menú de “Información”
const char *infoItems[] = {"Version", "Bateria", "Memoria", "Volver"};
const uint16_t infoIcons[] = {
    0x0085, // version
    0x00D1, // batería
    0x0093, // memoria
    0x01A9  // volver
};

Menu menus[] = {
    {topItems, topIcons, sizeof(topItems) / sizeof(topItems[0])},   // 0: Main
    {SubItems, SubIcons, sizeof(SubItems) / sizeof(SubItems[0])},   // 1: Opciones
    {msgItems, msgIcons, sizeof(msgItems) / sizeof(msgItems[0])},   // 2: Mensajes
    {cfgItems, cfgIcons, sizeof(cfgItems) / sizeof(cfgItems[0])},   // 3: Configuración
    {infoItems, infoIcons, sizeof(infoItems) / sizeof(infoItems[0])} // 4: Info
};

uint8_t menuDepth = 0; // 0 = principal, 1+ = submenus
uint8_t menuIndex = 0; // Índice seleccionado

// Guard de acciones UI para evitar dobles disparos por rebote/eventos solapados.
static uint32_t uiLastActionMs = 0;
const uint32_t UI_ACTION_GUARD_MS = 70;
// Evita que un long click dispare también un click corto al soltar.
static uint32_t btn2LastLongMs = 0;

static bool uiCanHandleAction() {
  uint32_t now = millis();
  if (now - uiLastActionMs < UI_ACTION_GUARD_MS)
    return false;
  uiLastActionMs = now;
  return true;
}

// --- Button Instances (defined in main but used here) ---
// Declared extern in main helpers if needed, but we can access valid objects if
// they are global. We will define specific handler functions here that main
// will attach.

// --- UI Helper Functions ---
// Devuelve hora formateada HH:MM:SS desde RTC para cabecera OLED.
// Si RTC falla, entrega placeholder seguro.
String getClockTime() {
  if (!rtcOK)
    return "??:??:??";
  DateTime now = rtc.now();
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.hour(), now.minute(),
           now.second());
  return String(buf);
}

// Convierte voltaje de batería a porcentaje aproximado de carga.
// Usa umbrales operativos del sistema para evitar valores irreales.
int calcBatteryPercent(float v) {
  if (v >= 4.1)
    return 100; // 4.2V = 100%
  if (v <= 3.3)
    return 0;                          // 3.4V = 0% (límite operacional ESP32)
  return (int)((v - 3.4) / 0.8 * 100); // Rango: 3.4V-4.2V = 0.8V
}

// Dibuja icono de batería dinámico en OLED según voltaje estimado.
// Incluye fallback visual para estado crítico/sin lectura válida.
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

// Dibuja un indicador mínimo de estado para TX/SD sin ocupar mucho header.
// enabled=feature ON, active=actividad reciente, ok=último resultado.
void drawActivityDot(int x, bool enabled, bool active, bool ok) {
  int y = 5;
  if (!enabled) {
    u8g2.drawCircle(x, y, 2);
    return;
  }

  if (active)
    u8g2.drawDisc(x, y, 2);
  else
    u8g2.drawCircle(x, y, 2);

  if (!ok) {
    u8g2.drawPixel(x + 3, y - 3);
  }
}

void drawHeader() {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 9, getClockTime().c_str());

  // Indicadores críticos mínimos (TX/SD) para no romper layout del header.
  uint32_t now = millis();
  bool txActive = (now - lastHttpActivityMs) < 1200;
  bool sdActive = (now - lastSdActivityMs) < 1200;
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(24, 9, "T");
  u8g2.drawStr(33, 9, "S");
  drawActivityDot(30, streaming, txActive, lastHttpOk);
  drawActivityDot(39, loggingEnabled, sdActive, lastSdOk);

  // Satellite icon + satélites (movido +10 px para evitar solape)
  if (haveFix && gpsStatus == "Fix") {
    u8g2.drawXBMP(62, 1, 8, 8, satelit_bitmap);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(70, 9);
    String sats = satellitesStr;
    if (sats.length() > 2)
      sats = sats.substring(0, 2);
    u8g2.print(sats);
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(60, 9, 0x0118);
  }

  // WiFi/Signal (movido +10 px para evitar solape)
  bool networkError = (csq == 99);
  if (networkError) {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(82, 9, 0x0118);
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(82, 9, 0x00FD);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(90, 9);
    String csqStr = String(csq);
    if (csqStr.length() > 2)
      csqStr = csqStr.substring(0, 2);
    u8g2.print(csqStr);
  }

  // Batería al extremo derecho.
  drawBatteryDynamic(110, 3, batV);
}

// Dibuja indicadores de paginación del menú en el footer OLED.
// Marca visualmente el item seleccionado.
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

// Presenta valor grande de sensor según pantalla activa (PM/Temp/Hum).
// Centra texto y etiqueta para lectura rápida en terreno.
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

// Dibuja item de menú con icono y texto centrados.
// Reutiliza estructuras de menú para mantener UI modular.
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

// Render principal de OLED con estado normal y estados transitorios.
// Integra cabecera, cuerpo de menú y footer en cada refresco.
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

// Muestra aviso visual y reinicia el ESP32 de forma controlada.
// Se ejecuta desde menú de configuración.
void handleRestart() {
  u8g2.clearBuffer();
  u8g2.drawStr(30, 30, "REINICIANDO...");
  u8g2.sendBuffer();
  delay(1000);
  ESP.restart();
}

// Alterna modo WiFi AP para gestión de archivos en SD.
// Inicia o detiene servidor web según estado actual.
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
// Evento BTN1: avanza selección en el menú activo.
// Reactiva OLED si estaba en ahorro de energía.
void ui_btn1_click() {
  if (!uiCanHandleAction())
    return;
  menuIndex = (menuIndex + 1) % menus[menuDepth].count;
  lastOledActivity = millis();
  if (config.oledAutoOff)
    u8g2.setPowerSave(0);
  renderDisplay();
}

// BTN2 Click: Select / Enter
// Evento BTN2 corto: entra/selecciona opciones del menú.
// Controla navegación entre niveles y acciones no críticas.
void ui_btn2_click() {
  if (millis() - btn2LastLongMs < 400)
    return;
  if (!uiCanHandleAction())
    return;
  lastOledActivity = millis();
  if (config.oledAutoOff)
    u8g2.setPowerSave(0);

  if (menuDepth == 0) {
    // Main Menu: click corto navega/entra, pero NO activa muestreo.
    if (menuIndex == 4) { // Opciones
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
    } else if (menuIndex == 2) { // Informacion
      menuDepth = 4;
      menuIndex = 0;
    } else if (menuIndex == 3) { // Volver
      menuDepth = 0;
      menuIndex = 0;
    }
  } else if (menuDepth == 2) {
    // Mensajes: mantenerse en submenú al seleccionar acción (como menú base).
    if (menuIndex < 3) {
      Serial.println(String("[UI] Mensaje seleccionado: ") + menus[2].items[menuIndex]);
    } else {
      menuDepth = 1;
      menuIndex = 0;
    }
  } else if (menuDepth == 3) {
    // Configuration Menu
    if (menuIndex == 0) { // REDES (placeholder)
      Serial.println("[CFG] REDES");
    } else if (menuIndex == 1) { // GUARDADO (placeholder)
      Serial.println("[CFG] GUARDADO");
    } else if (menuIndex == 2) { // RTC (placeholder)
      Serial.println("[CFG] RTC");
    } else if (menuIndex == 3) { // Reiniciar
      handleRestart();
    } else if (menuIndex == 4) { // Volver
      menuDepth = 1;
      menuIndex = 0;
    }
  } else if (menuDepth == 4) {
    // Información
    if (menuIndex == 0) {
      Serial.println(String("[INFO] Version: ") + VERSION);
    } else if (menuIndex == 1) {
      Serial.println(String("[INFO] Bateria V=") + String(batV, 2));
    } else if (menuIndex == 2) {
      Serial.println(String("[INFO] Mem free=") + String(ESP.getFreeHeap()));
    } else if (menuIndex == 3) {
      menuDepth = 1;
      menuIndex = 0;
    }
  } else {
    // Generic Back for other menus
    if (strcmp(menus[menuDepth].items[menuIndex], "Volver") == 0) {
      menuDepth--;
      menuIndex = 0;
    }
  }

  renderDisplay();
}

// BTN2 Hold: Action / Back
// Evento BTN2 largo: start/stop del flujo principal en pantalla raíz.
// En submenús actúa como retorno rápido al nivel anterior.
void ui_btn2_hold() {
  btn2LastLongMs = millis();
  if (!uiCanHandleAction())
    return;
  lastOledActivity = millis();
  if (config.oledAutoOff)
    u8g2.setPowerSave(0);

  if (menuDepth == 0) {
    // HOLD en raíz solo controla muestreo cuando está seleccionado "Empezar Muestreo".
    if (menuIndex != 3) {
      return;
    }

    if (streaming) {
      streaming = false;
      loggingEnabled = false;
      Serial.println("[UI] User Request: STOP Streaming/Logging");
      prefs.begin("system", false);
      prefs.putBool("streaming", false);
      prefs.end();
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_open_iconic_all_4x_t);
      u8g2.drawGlyph(48, 48, 0x00F9);
      u8g2.sendBuffer();
    } else {
      streaming = true;
      loggingEnabled = false;

      if (!SDOK) {
        spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
        SDOK = SD.begin(SD_CS, spiSD);
      }
      if (SDOK) {
        csvFileName = generateCSVFileName();
        prefs.begin("system", false);
        prefs.putString("csvFile", csvFileName);
        prefs.end();
      }

      Serial.println("[UI] User Request: START Streaming (logging OFF)");
      prefs.begin("system", false);
      prefs.putBool("streaming", true);
      prefs.end();

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_open_iconic_all_4x_t);
      u8g2.drawGlyph(48, 48, 0x00E9);
      u8g2.sendBuffer();
    }
  } else {
    // HOLD en submenú = salir (back), como en HIRI menu base.
    menuDepth--;
    menuIndex = 0;
    renderDisplay();
  }
}

// Placeholder de máquina de estados UI para futuras extensiones.
// Actualmente el estado se actualiza en handlers y renderDisplay().
void updateDisplayStateMachine() {
  // Nothing to update state-wise here, handled in renderDisplay and event
  // handlers
}

