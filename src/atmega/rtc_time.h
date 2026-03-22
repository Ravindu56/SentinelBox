// rtc_time.h — DS3231 RTC module
#pragma once
#include <Arduino.h>
#include <RTClib.h>

namespace RtcTime {
  bool init();
  // Fills buf with "YYYY-MM-DD HH:MM:SS\0"  (needs ≥20 chars)
  void nowStr(char *buf, uint8_t bufLen);
  // Returns Unix-style seconds since 2000 (for comparisons)
  uint32_t nowSec();
  bool available();
}
