#include "gps_parser.h"

static HardwareSerial _gpsSerial(2);
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
