// =====================================================================
// comms.cpp — ATmega328P ↔ ESP32 UART Protocol
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
//
// ATmega → ESP32 frames:
//   TEL,ts,tempC,hum,water,mq2,flame,vib,panic,flags,battV,battSt
//   EVT,ts,flagText,gps
//   PWR,MAINS|BATT|CRIT
//   SLEEP
//   WAKE
//   PONG  (reply to ESP32 PING)
//
// ESP32 → ATmega frames:
//   GPS,lat,lon,sats
//   PING
//   ACK,<millis>
//   SLEEP  (ESP32 requests ATmega hold — reserved)
//   TIME,YYYY-MM-DDTHH:MM:SS  (GPS time sync)
// =====================================================================
#include "comms.h"
#include "rtc_time.h"
#include "../../include/config.h"

// ── RX buffer ─────────────────────────────────────────────────────────
static char    _gps[32]    = "0.000000,0.000000";
static bool    _gpsValid   = false;
static char    _rxLine[128];
static uint8_t _rxIdx      = 0;

// ── ACK tracking ──────────────────────────────────────────────────────
static uint32_t _lastAckMs    = 0;   // millis() when last ACK was received
static bool     _esp32Replied = false;

// ─────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────
void Comms::init() {
    // Hardware Serial shared with USB on ATmega328P (D0/D1)
    // Disconnect ESP32 RX/TX wires before uploading firmware
    Serial.begin(115200);
}

// ─────────────────────────────────────────────────────────────────────
// tick — call every loop(); parses all incoming ESP32 frames
// ─────────────────────────────────────────────────────────────────────
void Comms::tick() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n') {
            _rxLine[_rxIdx] = '\0';
            // Strip trailing \r if present (Windows line endings)
            if (_rxIdx > 0 && _rxLine[_rxIdx - 1] == '\r')
                _rxLine[--_rxIdx] = '\0';

            _processLine();
            _rxIdx = 0;

        } else if (_rxIdx < sizeof(_rxLine) - 1) {
            _rxLine[_rxIdx++] = c;
        } else {
            _rxIdx = 0;  // buffer overflow — reset silently
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// _processLine — dispatch a complete received line
// ─────────────────────────────────────────────────────────────────────
void Comms::_processLine() {
    // ── GPS,lat,lon,sats ─────────────────────────────────────────────
    if (strncmp(_rxLine, PROTO_GPS ",", 4) == 0) {
        char *p1 = strchr(_rxLine + 4, ',');
        char *p2 = p1 ? strchr(p1 + 1, ',') : nullptr;
        if (p1 && p2) {
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
        return;
    }

    // ── PING → reply PONG ────────────────────────────────────────────
    if (strcmp(_rxLine, PROTO_PING) == 0) {
        Serial.println(F(PROTO_PONG));
        return;
    }

    // ── ACK,<millis> — ESP32 acknowledges a frame ────────────────────
    if (strncmp(_rxLine, PROTO_ACK ",", 4) == 0) {
        _lastAckMs    = millis();
        _esp32Replied = true;
        return;
    }

    // ── TIME,YYYY-MM-DDTHH:MM:SS — GPS time sync to DS3231 ──────────
    if (strncmp(_rxLine, "TIME,", 5) == 0) {
        RtcTime::setFromGPS(_rxLine + 5);
        return;
    }

    // ── SLEEP — ESP32 requesting ATmega to acknowledge sleep ─────────
    // (reserved for future bidirectional coordination)
    if (strcmp(_rxLine, "SLEEP") == 0) {
        // ATmega acknowledges; actual sleep is managed by power.cpp
        Serial.println(F(PROTO_ACK ",0"));
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────
// sendTel — TEL frame with full telemetry row
// ─────────────────────────────────────────────────────────────────────
void Comms::sendTel(const char *ts, const SensorData &d,
                    uint8_t flags, float battV, const char *battSt) {
    // Format: TEL,ts,tempC,hum,water,mq2,flame,vib,panic,flags,battV,battSt
    Serial.print(F(PROTO_TEL ",")); Serial.print(ts);         Serial.print(',');
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

// ─────────────────────────────────────────────────────────────────────
// sendEvt — EVT frame on hazard detection
// ─────────────────────────────────────────────────────────────────────
void Comms::sendEvt(const char *ts, const char *flagText, const char *gps) {
    // Format: EVT,ts,flagText,gps
    Serial.print(F(PROTO_EVT ",")); Serial.print(ts);
    Serial.print(',');              Serial.print(flagText);
    Serial.print(',');              Serial.println(gps);
}

// ─────────────────────────────────────────────────────────────────────
// sendSleep — tell ESP32 to enter deep sleep
// ─────────────────────────────────────────────────────────────────────
void Comms::sendSleep() {
    Serial.println(F(PROTO_SLEEP));
    Serial.flush();   // ensure byte leaves UART buffer before ATmega sleeps
}

// ─────────────────────────────────────────────────────────────────────
// sendWake — tell ESP32 it has been woken (sent after GPIO2 pulse)
// ─────────────────────────────────────────────────────────────────────
void Comms::sendWake() {
    Serial.println(F(PROTO_WAKE));
    Serial.flush();
}

// ─────────────────────────────────────────────────────────────────────
// sendPwr — notify ESP32 of current power state
// st: "MAINS" | "BATT" | "CRIT"
// ─────────────────────────────────────────────────────────────────────
void Comms::sendPwr(const char *st) {
    Serial.print(F(PROTO_PWR ","));
    Serial.println(st);
    Serial.flush();
}

// ─────────────────────────────────────────────────────────────────────
// ACK diagnostics — optional, used by main.cpp for ESP32 health check
// ─────────────────────────────────────────────────────────────────────
uint32_t Comms::lastAckAge() {
    if (!_esp32Replied) return 0xFFFFFFFF;  // never received
    return millis() - _lastAckMs;
}

bool Comms::esp32Responsive() {
    // ESP32 is considered responsive if an ACK came within last 30s
    return (_esp32Replied && lastAckAge() < 30000UL);
}

// ─────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────
const char* Comms::lastGPS()  { return _gps; }
bool        Comms::gpsValid() { return _gpsValid; }
