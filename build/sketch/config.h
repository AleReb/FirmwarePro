#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro\\config.h"
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// -------------------- Configuration System --------------------
struct SystemConfig {
  // SD Card
  bool sdAutoMount;      // Montar SD en boot (default: false)
  uint32_t sdSavePeriod; // Período guardado SD en ms (default: 3000)

  // HTTP Transmission
  uint32_t httpSendPeriod; // Período transmisión en ms (default: 3000)
  uint16_t httpTimeout;    // Timeout HTTP en segundos (default: 15)

  // Display OLED
  bool oledAutoOff;     // Apagar OLED automáticamente (default: false)
  uint32_t oledTimeout; // Timeout en ms (default: 120000 = 2min)

  // Power Management
  bool ledEnabled;       // NeoPixel habilitado (default: true)
  uint8_t ledBrightness; // Brillo LED: 10, 25, 50, 100 (default: 50%)

  // Autostart
  bool autostart; // Iniciar streaming/logging al encender (default: false)
  bool autostartWaitGps; // Esperar GPS fix antes de iniciar (default: false)
  uint16_t
      autostartGpsTimeout; // Timeout GPS en segundos (default: 600 = 10min)

  // GNSS Mode
  uint8_t gnssMode; // Modo GNSS: 1=GPS, 3=GPS+GLO, 5=GPS+BDS, 7=GPS+GLO+BDS,
                    // 15=ALL (default: 15)
};

// -------------------- Display State Machine --------------------
enum DisplayState { DISP_NORMAL, DISP_SD_SAVED };

#endif
