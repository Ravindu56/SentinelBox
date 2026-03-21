#include "gsm.h"

// ── Serial backend — Serial1 on Mega, SoftwareSerial on 328P ─────────
#ifdef SIM_USE_SERIAL1
  #define _sim  Serial1
  static void _simBegin(uint32_t baud) { Serial1.begin(baud); }
  static void _simEnd()                { Serial1.end(); }
#else
  #include <SoftwareSerial.h>
  static SoftwareSerial _simSW(SIM_RX, SIM_TX);
  #define _sim  _simSW
  static void _simBegin(uint32_t baud) { _simSW.begin(baud); }
  static void _simEnd()                { _simSW.end(); }
#endif

static bool    _ok  = false;
static char    _lastLine[64];
static char    _rxBuf[64];
static uint8_t _rxIdx = 0;

static bool _tryBaud(uint32_t baud) {
    _simEnd();
    delay(50);
    _simBegin(baud);
    delay(200);
    while (_sim.available()) _sim.read();
    _sim.println(F("AT"));
    unsigned long t0 = millis();
    String r = "";
    while (millis() - t0 < 1200) {
        while (_sim.available()) r += (char)_sim.read();
        if (r.indexOf(F("OK")) >= 0) return true;
    }
    return false;
}

void GSM::init() {
    static const uint32_t bauds[] = {9600, 19200, 38400, 57600, 115200};
    for (uint8_t i = 0; i < 5; i++) {
        if (_tryBaud(bauds[i])) {
            _sim.println(F("ATE0"));
            delay(200);
            _ok = true;
            return;
        }
    }
    _ok = false;
}

bool GSM::available() { return _ok; }

bool GSM::sendCmd(const char* cmd, const char* expect, uint16_t tMs) {
    _sim.println(cmd);
    unsigned long t0 = millis();
    String r = "";
    while (millis() - t0 < tMs) {
        while (_sim.available()) {
            r += (char)_sim.read();
            if (r.indexOf(expect) >= 0) return true;
        }
    }
    return false;
}

bool GSM::sendCmd(const __FlashStringHelper* cmd, const char* expect, uint16_t tMs) {
    _sim.println(cmd);
    unsigned long t0 = millis();
    String r = "";
    while (millis() - t0 < tMs) {
        while (_sim.available()) {
            r += (char)_sim.read();
            if (r.indexOf(expect) >= 0) return true;
        }
    }
    return false;
}

bool GSM::sendSMS(const char* number, const char* msg) {
    if (!_ok) return false;
    if (!sendCmd(F("AT+CMGF=1"), "OK", 2000)) return false;
    char buf[24];
    snprintf(buf, sizeof(buf), "AT+CMGS=\"%s\"", number);
    _sim.println(buf);
    delay(300);
    _sim.print(msg);
    _sim.write(26);  // Ctrl+Z
    unsigned long t0 = millis();
    String r = "";
    while (millis() - t0 < 10000) {
        while (_sim.available()) {
            r += (char)_sim.read();
            if (r.indexOf("+CMGS:") >= 0) return true;
            if (r.indexOf("ERROR")  >= 0) return false;
        }
    }
    return false;
}

void GSM::tick() {
    while (_sim.available()) {
        char c = _sim.read();
        if (c == '\n') {
            _rxBuf[_rxIdx] = '\0';
            if (_rxIdx > 0 && _rxBuf[_rxIdx-1] == '\r')
                _rxBuf[--_rxIdx] = '\0';
            if (_rxIdx > 0) strlcpy(_lastLine, _rxBuf, sizeof(_lastLine));
            _rxIdx = 0;
        } else if (_rxIdx < sizeof(_rxBuf) - 1) {
            _rxBuf[_rxIdx++] = c;
        }
    }
}

const char* GSM::lastLine()     { return _lastLine; }
void        GSM::clearLastLine() { _lastLine[0] = '\0'; }
