#include "rtc_time.h"
#include <Wire.h>

static RTC_DS3231 _rtc;
static bool       _ok = false;

bool RtcTime::init() {
    if (!_rtc.begin()) { _ok = false; return false; }

    // Only overwrite RTC with compile-time stamp if it genuinely lost power
    // AND the compile-time date is more recent than what the RTC holds.
    // This prevents rolling back a GPS-synced clock after a brief power cut.
    if (_rtc.lostPower()) {
        DateTime compileTime(F(__DATE__), F(__TIME__));
        DateTime rtcNow = _rtc.now();
        if (compileTime.unixtime() > rtcNow.unixtime()) {
            _rtc.adjust(compileTime);
        }
    }

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
//
// BUG FIX: avr-libc sscanf does NOT support the %hhu specifier
// (unsigned char via sscanf). It silently matches 0 for every field,
// so the clock was being set to 2000-00-00T00:00:00 on every GPS sync.
// Fix: parse each field manually with strtol / atoi on substring copies.
//
// Format: "YYYY-MM-DDTHH:MM:SS"
//          0123456789012345678
// ─────────────────────────────────────────────────────────────────────
void RtcTime::setFromGPS(const char *isoStr) {
    if (!_ok || !isoStr) return;

    uint8_t len = (uint8_t)strlen(isoStr);
    if (len < 19) return;

    // Validate separator positions to confirm format
    if (isoStr[4]  != '-' || isoStr[7]  != '-' ||
        isoStr[10] != 'T' || isoStr[13] != ':' ||
        isoStr[16] != ':') return;

    // Extract each field using a small stack buffer + atoi
    // atoi is safe here — we've already verified the positions
    char tmp[5];

    // Year (4 digits)
    memcpy(tmp, isoStr + 0, 4); tmp[4] = '\0';
    uint16_t yr = (uint16_t)atoi(tmp);

    // Month (2 digits)
    memcpy(tmp, isoStr + 5, 2); tmp[2] = '\0';
    uint8_t mo = (uint8_t)atoi(tmp);

    // Day
    memcpy(tmp, isoStr + 8, 2); tmp[2] = '\0';
    uint8_t dy = (uint8_t)atoi(tmp);

    // Hour
    memcpy(tmp, isoStr + 11, 2); tmp[2] = '\0';
    uint8_t hr = (uint8_t)atoi(tmp);

    // Minute
    memcpy(tmp, isoStr + 14, 2); tmp[2] = '\0';
    uint8_t mn = (uint8_t)atoi(tmp);

    // Second
    memcpy(tmp, isoStr + 17, 2); tmp[2] = '\0';
    uint8_t sc = (uint8_t)atoi(tmp);

    // Sanity bounds
    if (yr < 2024 || yr > 2099) return;
    if (mo < 1    || mo > 12)   return;
    if (dy < 1    || dy > 31)   return;
    if (hr > 23 || mn > 59 || sc > 59) return;

    _rtc.adjust(DateTime(yr, mo, dy, hr, mn, sc));
}
