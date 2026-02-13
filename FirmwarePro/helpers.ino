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
