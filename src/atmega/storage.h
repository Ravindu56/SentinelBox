// storage.h — Micro SD CSV logging (no EEPROM)
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "sensors.h"
#include "../../include/config.h"

namespace Storage {
  bool init();
  bool available();
  // Appends one data row; flushes every SD_FLUSH_EVERY rows
  bool logRow(const char *ts, const SensorData &d,
              uint8_t flags, float battV, const char *battSt,
              const char *gps);
  // Force flush to card
  void flush();
}
