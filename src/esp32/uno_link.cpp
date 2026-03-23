#include "uno_link.h"

static HardwareSerial _unoSerial(1);  // UART1
static TelData        _tel;
static char           _evtBuf[80];
static char           _rxLine[160];
static uint8_t        _rxIdx       = 0;
static unsigned long  _lastGpsSend = 0;

// Power coordination state
static bool _sleepReq       = false;
static char _pwrStateBuf[8] = "";

// ── Parse comma-separated field N (0-indexed) ────────────────────────
static bool _field(const char *str, uint8_t n, char *out, uint8_t outLen) {
    const char *p = str;
    for (uint8_t i = 0; i < n; i++) {
        p = strchr(p, ',');
        if (!p) return false;
        p++;
    }
    const char *end = strchr(p, ',');
    uint8_t len = end ? (uint8_t)(end - p) : (uint8_t)strlen(p);
    if (len >= outLen) len = outLen - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    return true;
}

// ── Parse TEL,time,tempC,hum,water,mq2,flame,vib,panic,flags,battV,battSt
static void _parseTel(const char *line) {
    char tmp[12];
    if (_field(line, 1, _tel.ts,    sizeof(_tel.ts)))    {}
    if (_field(line, 2, tmp, sizeof(tmp)))  _tel.tempC    = atof(tmp);
    if (_field(line, 3, tmp, sizeof(tmp)))  _tel.humidity = atof(tmp);
    if (_field(line, 4, tmp, sizeof(tmp)))  _tel.water    = atoi(tmp);
    if (_field(line, 5, tmp, sizeof(tmp)))  _tel.mq2      = atoi(tmp);
    if (_field(line, 6, tmp, sizeof(tmp)))  _tel.flame    = atoi(tmp);
    if (_field(line, 7, tmp, sizeof(tmp)))  _tel.vib      = atoi(tmp);
    if (_field(line, 8, tmp, sizeof(tmp)))  _tel.panic    = atoi(tmp);
    if (_field(line, 9, tmp, sizeof(tmp)))  _tel.flags    = atoi(tmp);
    if (_field(line, 10, tmp, sizeof(tmp))) _tel.battV    = atof(tmp);
    if (_field(line, 11, _tel.battSt, sizeof(_tel.battSt))) {}
    _tel.fresh = true;
}

// ─────────────────────────────────────────────────────────────────────
void UnoLink::init() {
    _unoSerial.begin(115200, SERIAL_8N1, UNO_RX_PIN, UNO_TX_PIN);
    memset(&_tel, 0, sizeof(_tel));
    _evtBuf[0]      = '\0';
    _pwrStateBuf[0] = '\0';
    _sleepReq       = false;
}

void UnoLink::tick() {
    _tel.fresh = false;

    while (_unoSerial.available()) {
        char c = _unoSerial.read();
        if (c == '\n') {
            _rxLine[_rxIdx] = '\0';
            if (_rxIdx > 0 && _rxLine[_rxIdx - 1] == '\r')
                _rxLine[--_rxIdx] = '\0';

            if      (strncmp(_rxLine, "TEL,",  4) == 0) _parseTel(_rxLine + 4);
            else if (strncmp(_rxLine, "EVT,",  4) == 0) strlcpy(_evtBuf, _rxLine + 4, sizeof(_evtBuf));
            else if (strncmp(_rxLine, "PWR,",  4) == 0) strlcpy(_pwrStateBuf, _rxLine + 4, sizeof(_pwrStateBuf));
            else if (strcmp (_rxLine, "SLEEP")    == 0) _sleepReq = true;
            else if (strcmp (_rxLine, "PING")     == 0) _unoSerial.println("PONG");
            // WAKE frame — ESP32 side needs no action

            _rxIdx = 0;
        } else if (_rxIdx < sizeof(_rxLine) - 1) {
            _rxLine[_rxIdx++] = c;
        } else {
            _rxIdx = 0; // overflow — discard
        }
    }

    // Send GPS to ATmega on interval
    unsigned long now = millis();
    if (now - _lastGpsSend >= GPS_SEND_INTERVAL_MS) {
        _lastGpsSend = now;
        sendGPS();
    }
}

void UnoLink::sendGPS() {
    char coords[32];
    GpsParser::coordStr(coords, sizeof(coords));
    char buf[48];
    snprintf(buf, sizeof(buf), "GPS,%s,%u", coords, GpsParser::sats());
    _unoSerial.println(buf);
}

void UnoLink::sendAck() {
    char buf[20];
    snprintf(buf, sizeof(buf), "ACK,%lu", millis());
    _unoSerial.println(buf);
}

void UnoLink::sendRaw(const char *line) {
    _unoSerial.println(line);
}

const TelData& UnoLink::telemetry()      { return _tel; }
const char*    UnoLink::lastEvent()      { return _evtBuf; }
void           UnoLink::clearEvent()     { _evtBuf[0] = '\0'; }

bool           UnoLink::sleepRequested()    { return _sleepReq; }
void           UnoLink::clearSleepRequest() { _sleepReq = false; }
const char*    UnoLink::lastPwrState()      { return _pwrStateBuf; }
void           UnoLink::clearPwrState()     { _pwrStateBuf[0] = '\0'; }
