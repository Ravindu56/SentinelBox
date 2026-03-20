#include "leds.h"

static struct {
  uint16_t onMs;
  uint8_t  times;
  uint8_t  phase;          // 0=off, 1=on
  unsigned long lastMs;
} _beep;

void Leds::init() {
  pinMode(PIN_LED_R,  OUTPUT);
  pinMode(PIN_LED_G,  OUTPUT);
  pinMode(PIN_LED_B,  OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED_G, HIGH);  // green at boot
}

static inline void _set(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r ? HIGH : LOW);
  digitalWrite(PIN_LED_G, g ? HIGH : LOW);
  digitalWrite(PIN_LED_B, b ? HIGH : LOW);
}

void Leds::update(uint8_t f) {
  if      (f == 0)           _set(false, true,  false);  // green  = normal
  else if (f & HZ_PANIC)     _set(true,  false, true);   // magenta= panic
  else if (f & HZ_FIRE)      _set(true,  false, false);  // red    = fire
  else if (f & HZ_GAS)       _set(true,  true,  false);  // yellow = gas
  else if (f & HZ_FLOOD)     _set(false, false, true);   // blue   = flood
  else                       _set(true,  false, false);  // red    = other
}

void Leds::beep(uint16_t ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
}

void Leds::beepAsync(uint16_t onMs, uint8_t times) {
  _beep.onMs   = onMs;
  _beep.times  = times * 2;  // on+off pairs
  _beep.phase  = 0;
  _beep.lastMs = 0;
}

void Leds::tickAsync() {
  if (_beep.times == 0) return;
  unsigned long now = millis();
  if (now - _beep.lastMs < _beep.onMs) return;
  _beep.lastMs = now;
  _beep.phase  = !_beep.phase;
  digitalWrite(PIN_BUZZER, _beep.phase ? HIGH : LOW);
  _beep.times--;
  if (_beep.times == 0) digitalWrite(PIN_BUZZER, LOW);
}
