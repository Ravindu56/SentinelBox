#include "leds.h"

// ── Passive buzzer frequencies per hazard ─────────────────────────────
// tone() uses Timer2 — no conflict with SoftwareSerial or SD
#define FREQ_BOOT    1000   // short low beep at startup
#define FREQ_NORMAL     0   // silent
#define FREQ_PANIC   3000   // high urgent
#define FREQ_FIRE    2800   // high fast
#define FREQ_GAS     2000   // mid warning
#define FREQ_FLOOD   1500   // low warning
#define FREQ_OTHER   2200   // generic alert

// ── Async beep state machine ──────────────────────────────────────────
static struct {
    uint16_t     onMs;
    uint16_t     offMs;
    uint8_t      times;
    uint8_t      phase;       // 0=off, 1=on
    unsigned long lastMs;
    uint16_t     freq;        // Hz for passive buzzer
} _beep;

// ── Init ──────────────────────────────────────────────────────────────
void Leds::init() {
    pinMode(PIN_LED_R,  OUTPUT);
    pinMode(PIN_LED_G,  OUTPUT);
    pinMode(PIN_LED_B,  OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_LED_G, HIGH);  // green at boot
    noTone(PIN_BUZZER);             // ensure silent at start
}

// ── LED helper ────────────────────────────────────────────────────────
static inline void _set(bool r, bool g, bool b) {
    digitalWrite(PIN_LED_R, r ? HIGH : LOW);
    digitalWrite(PIN_LED_G, g ? HIGH : LOW);
    digitalWrite(PIN_LED_B, b ? HIGH : LOW);
}

// ── LED + continuous tone for active hazard ───────────────────────────
void Leds::update(uint8_t f) {
    if (f == 0) {
        _set(false, true, false);   // green = normal
        noTone(PIN_BUZZER);         // silent when normal
    }
    else if (f & HZ_PANIC) {
        _set(true, false, true);    // magenta = panic
        tone(PIN_BUZZER, FREQ_PANIC);
    }
    else if (f & HZ_FIRE) {
        _set(true, false, false);   // red = fire
        tone(PIN_BUZZER, FREQ_FIRE);
    }
    else if (f & HZ_GAS) {
        _set(true, true, false);    // yellow = gas
        tone(PIN_BUZZER, FREQ_GAS);
    }
    else if (f & HZ_FLOOD) {
        _set(false, false, true);   // blue = flood
        tone(PIN_BUZZER, FREQ_FLOOD);
    }
    else {
        _set(true, false, false);   // red = other
        tone(PIN_BUZZER, FREQ_OTHER);
    }
}

// ── Blocking beep — boot confirmation only ───────────────────────────
void Leds::beep(uint16_t ms) {
    tone(PIN_BUZZER, FREQ_BOOT);
    delay(ms);
    noTone(PIN_BUZZER);
}

// ── Async beep — non-blocking, queued ────────────────────────────────
// onMs = tone duration, times = number of beeps
void Leds::beepAsync(uint16_t onMs, uint8_t times) {
    _beep.freq   = FREQ_OTHER;      // default freq; override if needed
    _beep.onMs   = onMs;
    _beep.offMs  = onMs;            // equal on/off gap
    _beep.times  = times * 2;       // on+off counted as pairs
    _beep.phase  = 0;
    _beep.lastMs = 0;
}

// Overload: specify frequency explicitly
void Leds::beepAsync(uint16_t onMs, uint8_t times, uint16_t freq) {
    _beep.freq   = freq;
    _beep.onMs   = onMs;
    _beep.offMs  = onMs;
    _beep.times  = times * 2;
    _beep.phase  = 0;
    _beep.lastMs = 0;
}

// ── tickAsync — call every loop() ─────────────────────────────────────
void Leds::tickAsync() {
    if (_beep.times == 0) return;

    unsigned long now = millis();
    uint16_t interval = (_beep.phase == 1) ? _beep.onMs : _beep.offMs;

    if (now - _beep.lastMs < interval) return;
    _beep.lastMs = now;
    _beep.phase  = !_beep.phase;

    if (_beep.phase == 1) {
        tone(PIN_BUZZER, _beep.freq);   // start tone
    } else {
        noTone(PIN_BUZZER);             // silence between beeps
    }

    _beep.times--;
    if (_beep.times == 0) noTone(PIN_BUZZER);  // ensure silence at end
}
