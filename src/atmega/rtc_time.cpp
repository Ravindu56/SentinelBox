#include "rtc_time.h"
#include <Wire.h>

static RTC_DS3231 _rtc;
static bool       _ok = false;

bool RtcTime::init() {
  if (!_rtc.begin()) { _ok = false; return false; }
  if (_rtc.lostPower()) _rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  _ok = true;
  return true;
}

void RtcTime::nowStr(char *buf, uint8_t bufLen) {
  if (!_ok) { strlcpy(buf, "0000-00-00 00:00:00", bufLen); return; }
  DateTime n = _rtc.now();
  snprintf(buf, bufLen, "%04u-%02u-%02u %02u:%02u:%02u",
           n.year(), n.month(), n.day(),
           n.hour(), n.minute(), n.second());
}

uint32_t RtcTime::nowSec() {
  if (!_ok) return 0;
  return _rtc.now().unixtime();
}

bool RtcTime::available() { return _ok; }
