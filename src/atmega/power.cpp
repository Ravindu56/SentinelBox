// power.cpp — Power state machine, sleep, WDT, battery monitor
#include "power.h"
#include <avr/sleep.h>
#include "storage.h" 
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>

// ── WDT ISR ──────────────────────────────────────────────────────────
// Interrupt mode (WDIE set) — MCU wakes from sleep, does NOT reset
ISR(WDT_vect) {
    // intentionally empty — just wakes the CPU
}

// ── INT0 ISR (D2) ─────────────────────────────────────────────────────
// Fired by ESP32 pulling D2 HIGH to request ATmega wake
static volatile bool _int0Fired = false;
ISR(INT0_vect) {
    _int0Fired = true;
}

// ── Internal: set WDT to interrupt-only mode (no reset), 8s period ───
static void _wdtStartInterrupt() {
    cli();
    wdt_reset();
    // Set WDCE + WDE to unlock WDTCSR, then configure WDIE (interrupt) not WDE (reset)
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR  = (1 << WDIE) | (1 << WDP3) | (1 << WDP0);  // 8s, interrupt mode
    sei();
}

// ── Internal: disable WDT entirely ────────────────────────────────────
static void _wdtStop() {
    cli();
    wdt_reset();
    MCUSR &= ~(1 << WDRF);
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR  = 0x00;
    sei();
}

// ── Internal: re-enable WDT in reset mode (8s) for system safety ──────
static void _wdtStartReset() {
    cli();
    wdt_reset();
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR  = (1 << WDE) | (1 << WDP3) | (1 << WDP0);  // 8s reset mode
    sei();
}

// ── Public: init WDT in reset mode (called from setup) ────────────────
void Power::initWatchdog() {
    _wdtStartReset();
}

// ── Public: feed WDT (called from main loop) ──────────────────────────
void Power::feedWatchdog() {
    wdt_reset();
}

// ── Public: sleepMs — stacked 8s WDT-driven power-down sleep ─────────
// Each WDT wake checks elapsed time and sleeps again if needed.
// If wakeOnInt=true, INT0 on D2 can break the sleep early.
void Power::sleepMs(uint32_t ms, bool wakeOnInt) {
    uint32_t elapsed = 0;
    const uint32_t WDT_PERIOD_MS = 8000UL;

    // Enable INT0 (D2) as LOW-level wake source if requested
    if (wakeOnInt) {
        _int0Fired = false;
        pinMode(PIN_ESP_WAKE, INPUT);           // D2 = input, ESP32 drives it
        EICRA &= ~((1 << ISC01) | (1 << ISC00)); // INT0 = low-level trigger
        EIMSK |=  (1 << INT0);                  // enable INT0
    }

    // Flush SD and UART before entering sleep
    Storage::flush();

    while (elapsed < ms) {
        // Switch WDT to interrupt mode (won't reset, just wakes)
        _wdtStartInterrupt();

        ADCSRA &= ~(1 << ADEN);            // disable ADC (~250 µA saved)
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        sei();
        sleep_cpu();                        // ── SLEEPING ──
        sleep_disable();
        ADCSRA |= (1 << ADEN);             // re-enable ADC

        // Restore WDT to reset mode immediately after wake
        _wdtStartReset();
        wdt_reset();

        elapsed += WDT_PERIOD_MS;

        // Break early if INT0 fired (ESP32 or panic button triggered wake)
        if (wakeOnInt && _int0Fired) break;
    }

    // Disable INT0 after sleep sequence
    if (wakeOnInt) {
        EIMSK &= ~(1 << INT0);
    }
}

// ── Public: wakeESP32 ─────────────────────────────────────────────────
// Sends a short HIGH pulse on D2 → ESP32 GPIO2 (ext0 wakeup)
void Power::wakeESP32() {
    pinMode(PIN_ESP_WAKE, OUTPUT);
    digitalWrite(PIN_ESP_WAKE, HIGH);
    delay(ESP_WAKE_PULSE_MS);
    digitalWrite(PIN_ESP_WAKE, LOW);
    pinMode(PIN_ESP_WAKE, INPUT);   // release the pin after pulse
}

// ── Public: battery voltage (16-sample average) ───────────────────────
float Power::batteryVoltage() {
    long sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(PIN_BATTERY);
    float avg = sum / 16.0f;
    return avg * (5.0f / 1023.0f) * VOLTAGE_DIVIDER_RATIO;
}

// ── Public: batteryStatusStr ─────────────────────────────────────────
void Power::batteryStatusStr(char *buf, uint8_t bufLen) {
    if (isOnMains()) { strlcpy(buf, "CHARGING", bufLen); return; }
    float v = batteryVoltage();
    if      (v > 4.1f) strlcpy(buf, "FULL",     bufLen);
    else if (v < BATT_CRITICAL_V) strlcpy(buf, "LOW_BATT", bufLen);
    else               strlcpy(buf, "ON_BATT",  bufLen);
}

// ── Public: isCritical ────────────────────────────────────────────────
bool Power::isCritical() {
    return (!isOnMains() && batteryVoltage() < BATT_CRITICAL_V);
}

// ── Public: isOnMains ─────────────────────────────────────────────────
// TP4056 CHRG pin: ADC < 200 means actively charging (= mains present)
bool Power::isOnMains() {
    return (analogRead(PIN_TP_CHRG) < 200);
}

// ── Public: updateMode ───────────────────────────────────────────────
// Central power state machine. Returns the current mode.
PowerMode Power::updateMode(uint8_t hazardFlags) {
    bool mains    = isOnMains();
    bool hazard   = (hazardFlags != 0);
    bool critical = isCritical();

    if (hazard)    return PWR_MODE_EMERG;
    if (critical)  return PWR_MODE_CRIT;
    if (mains)     return PWR_MODE_MAINS;
    return          PWR_MODE_BATT;
}
