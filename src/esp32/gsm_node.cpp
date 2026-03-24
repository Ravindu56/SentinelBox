// gsm_node.cpp — SIM800L on ESP32 UART1
// GPIO13 = RX (ESP32 ← SIM800L TX)  — direct, SIM800L TX = 3.3V
// GPIO14 = TX (ESP32 → SIM800L RX)  — 1kΩ/2kΩ voltage divider to 2.2V
//
// NOTE: GPIO12 must NOT be used as RX — it is a strapping pin.
// SIM800L TX idles HIGH and pulls GPIO12 HIGH at boot,
// causing ESP32 to select 1.8V flash voltage → upload failure.
#include "gsm_node.h"

// UART1 — GPIO13(RX) / GPIO14(TX)
// Do NOT use GPIO12 (strapping), GPIO0 (boot), GPIO1/GPIO3 (Serial/USB)
#define GSM_RX_PIN      13
#define GSM_TX_PIN      14
#define GSM_BAUD        9600
#define GSM_INIT_WAIT   1500    // ms after begin before first AT

static bool     _ok      = false;
static char     _lastLine[64] = "";
static char     _rxBuf[64];
static uint8_t  _rxIdx   = 0;

// ── Internal: blocking wait for response string ───────────────────────
static bool _waitFor(const char *expect, uint16_t tMs) {
  unsigned long t0 = millis();
  String r;
  r.reserve(64);
  while (millis() - t0 < tMs) {
    while (Serial1.available()) {
      r += (char)Serial1.read();
      if (r.indexOf(expect) >= 0) return true;
    }
    yield();   // keep ESP32 WiFi stack alive during wait
  }
  return false;
}

// ── Internal: send command, wait for response ─────────────────────────
static bool _cmd(const char *c, const char *expect, uint16_t tMs = 2000) {
  while (Serial1.available()) Serial1.read();  // flush
  Serial1.println(c);
  return _waitFor(expect, tMs);
}

// ─────────────────────────────────────────────────────────────────────
void GsmNode::init() {
  // Serial1 = UART1, reassigned to GPIO13(RX) / GPIO14(TX)
  Serial1.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  delay(GSM_INIT_WAIT);
  while (Serial1.available()) Serial1.read();   // flush power-on junk

  Serial.println(F("[GSM]  Probing SIM800L on UART1 GPIO13(RX)/GPIO14(TX)..."));

  // Probe — try twice (SIM800L may need a nudge after power-on)
  bool alive = _cmd("AT", "OK", 3000) || _cmd("AT", "OK", 3000);
  if (!alive) {
    Serial.println(F("[GSM]  No response from SIM800L — check wiring/power"));
    _ok = false;
    return;
  }

  _cmd("ATE0",              "OK", 1000);   // echo off
  _cmd("AT+CMGF=1",         "OK", 1000);   // SMS text mode
  _cmd("AT+CNMI=2,0,0,0,0", "OK", 1000);  // suppress unsolicited SMS storage

  _ok = true;
  Serial.println(F("[GSM]  SIM800L ready"));
}

bool GsmNode::available() { return _ok; }

// ─────────────────────────────────────────────────────────────────────
bool GsmNode::sendSMS(const char *number, const char *msg) {
  if (!_ok) return false;

  char buf[32];
  snprintf(buf, sizeof(buf), "AT+CMGS=\"%s\"", number);
  Serial1.println(buf);
  if (!_waitFor(">", 5000)) return false;   // wait for prompt

  Serial1.print(msg);
  Serial1.write(26);         // Ctrl+Z — triggers send
  delay(100);

  bool sent = _waitFor("+CMGS:", 10000);
  Serial.println(sent ? F("[GSM]  SMS sent OK") : F("[GSM]  SMS FAILED"));
  return sent;
}

// ─────────────────────────────────────────────────────────────────────
// Non-blocking URC reader — call every loop()
void GsmNode::tick() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      _rxBuf[_rxIdx] = '\0';
      if (_rxIdx > 0 && _rxBuf[_rxIdx-1] == '\r') _rxBuf[--_rxIdx] = '\0';
      if (_rxIdx > 0) strlcpy(_lastLine, _rxBuf, sizeof(_lastLine));
      _rxIdx = 0;
    } else if (_rxIdx < sizeof(_rxBuf) - 1) {
      _rxBuf[_rxIdx++] = c;
    }
  }
}

const char* GsmNode::lastLine()      { return _lastLine; }
void        GsmNode::clearLastLine() { _lastLine[0] = '\0'; }
