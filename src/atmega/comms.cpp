#include "comms.h"
#include "../../include/config.h"

static char   _gps[32]   = "0.000000,0.000000";
static bool   _gpsValid  = false;
static char   _rxLine[128];
static uint8_t _rxIdx   = 0;

void Comms::init() {
  // Hardware Serial is shared with USB on ATmega328P (D0/D1)
  // Disconnect ESP32 TX/RX during programming
  Serial.begin(115200);
}

void Comms::tick() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      _rxLine[_rxIdx] = '\0';
      if (_rxIdx > 0 && _rxLine[_rxIdx-1] == '\r')
        _rxLine[--_rxIdx] = '\0';

      // Parse GPS,lat,lon,sats
      if (strncmp(_rxLine, PROTO_GPS ",", 4) == 0) {
        char *p1 = strchr(_rxLine + 4, ',');
        char *p2 = p1 ? strchr(p1 + 1, ',') : nullptr;
        if (p1 && p2) {
          // Extract lat,lon only (skip sats)
          uint8_t latLen = p1 - (_rxLine + 4);
          uint8_t lonLen = p2 - (p1 + 1);
          if (latLen > 0 && lonLen > 0 &&
              strncmp(_rxLine + 4, "0.000000", 8) != 0) {
            snprintf(_gps, sizeof(_gps), "%.*s,%.*s",
                     latLen, _rxLine + 4,
                     lonLen, p1 + 1);
            _gpsValid = true;
          }
        }
      }
      // Handle PING
      if (strcmp(_rxLine, PROTO_PING) == 0) {
        Serial.println(F(PROTO_PONG));
      }
      _rxIdx = 0;
    } else if (_rxIdx < sizeof(_rxLine) - 1) {
      _rxLine[_rxIdx++] = c;
    } else {
      _rxIdx = 0; // overflow reset
    }
  }
}

void Comms::sendTel(const char *ts, const SensorData &d,
                    uint8_t flags, float battV, const char *battSt) {
  // TEL,time,tempC,hum,water,mq2,flame,vib,panic,flags,battV,battSt
  Serial.print(F(PROTO_TEL ",")); Serial.print(ts);     Serial.print(',');
  if (!isnan(d.tempC))    Serial.print(d.tempC,    1); else Serial.print(F("E"));
  Serial.print(',');
  if (!isnan(d.humidity)) Serial.print(d.humidity, 1); else Serial.print(F("E"));
  Serial.print(',');
  Serial.print(d.water);  Serial.print(',');
  Serial.print(d.mq2);    Serial.print(',');
  Serial.print(d.flame);  Serial.print(',');
  Serial.print(d.vib);    Serial.print(',');
  Serial.print(d.panic ? 1 : 0); Serial.print(',');
  Serial.print(flags);    Serial.print(',');
  Serial.print(battV, 2); Serial.print(',');
  Serial.println(battSt);
}

void Comms::sendEvt(const char *ts, const char *flagText, const char *gps) {
  // EVT,time,flagText,gps
  Serial.print(F(PROTO_EVT ",")); Serial.print(ts);
  Serial.print(','); Serial.print(flagText);
  Serial.print(','); Serial.println(gps);
}

const char* Comms::lastGPS()  { return _gps; }
bool        Comms::gpsValid() { return _gpsValid; }
