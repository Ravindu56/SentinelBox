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

// ─────────────────────────────────────────────────────────────────────
// setFromGPS — parse "YYYY-MM-DDTHH:MM:SS" and write to DS3231
// Only updates if RTC is initialised and the string is well-formed.
// Called from Comms::_processLine() on receipt of "TIME," frame.
// ─────────────────────────────────────────────────────────────────────
void RtcTime::setFromGPS(const char *isoStr) {
    if (!_ok || !isoStr || strlen(isoStr) < 19) return;

    // Expected format: "YYYY-MM-DDTHH:MM:SS"
    //                   0123456789012345678
    uint16_t yr  = 0;
    uint8_t  mo  = 0, dy = 0;
    uint8_t  hr  = 0, mn = 0, sc = 0;

    // Use sscanf — safe because input length is already checked
    int matched = sscanf(isoStr, "%4u-%2hhu-%2hhuT%2hhu:%2hhu:%2hhu",
                         &yr, &mo, &dy, &hr, &mn, &sc);
    if (matched != 6) return;                    // malformed string
    if (yr < 2024 || mo < 1 || mo > 12) return; // sanity check

    _rtc.adjust(DateTime(yr, mo, dy, hr, mn, sc));
}
