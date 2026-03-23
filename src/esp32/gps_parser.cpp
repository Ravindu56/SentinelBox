#include "gps_parser.h"

static HardwareSerial _gpsSerial(2);   // UART2 — GPIO16 RX / GPIO17 TX
static TinyGPSPlus    _gps;

void GpsParser::init() {
    _gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void GpsParser::tick() {
    while (_gpsSerial.available())
        _gps.encode(_gpsSerial.read());
}

bool    GpsParser::valid() { return _gps.location.isValid(); }
float   GpsParser::lat()   { return _gps.location.lat(); }
float   GpsParser::lon()   { return _gps.location.lng(); }
uint8_t GpsParser::sats()  {
    return _gps.satellites.isValid() ? (uint8_t)_gps.satellites.value() : 0;
}

void GpsParser::coordStr(char *buf, uint8_t bufLen) {
    if (!valid()) { strlcpy(buf, "0.000000,0.000000", bufLen); return; }
    snprintf(buf, bufLen, "%.6f,%.6f", lat(), lon());
}

// ── hasTime ──────────────────────────────────────────────────────────
// Returns true only when TinyGPSPlus has a valid, updated date AND time
bool GpsParser::hasTime() {
    return _gps.date.isValid() && _gps.date.isUpdated() &&
           _gps.time.isValid() && _gps.time.isUpdated();
}

// ── timeStr ──────────────────────────────────────────────────────────
// Fills buf with "YYYY-MM-DDTHH:MM:SS\0"  (ISO 8601, needs ≥ 20 chars)
// Sets buf[0] = '\0' if no valid time fix is available
void GpsParser::timeStr(char *buf, uint8_t bufLen) {
    if (!hasTime() || bufLen < 20) { buf[0] = '\0'; return; }
    snprintf(buf, bufLen, "%04u-%02u-%02uT%02u:%02u:%02u",
             _gps.date.year(),
             _gps.date.month(),
             _gps.date.day(),
             _gps.time.hour(),
             _gps.time.minute(),
             _gps.time.second());
}
