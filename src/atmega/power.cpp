#include "power.h"
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>

// WDT ISR — presence prevents compiler removing WDE; resets system
ISR(WDT_vect) { /* system will reset on next WDT cycle */ }

void Power::initWatchdog() {
  cli();
  wdt_reset();
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  WDTCSR  = (1 << WDE) | (1 << WDP3) | (1 << WDP0); // 8 s reset mode
  sei();
}

void Power::feedWatchdog() { wdt_reset(); }

void Power::enterSleep() {
  ADCSRA &= ~(1 << ADEN);          // disable ADC (saves ~250 µA)
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sei();
  sleep_cpu();                      // halt until interrupt
  sleep_disable();
  ADCSRA |= (1 << ADEN);           // re-enable ADC
}

float Power:: batteryVoltage() {
    long sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(PIN_BATTERY);
    float avg = sum / 16.0;
    return avg * (5.0 / 1023.0) * VOLTAGE_DIVIDER_RATIO;
}


void Power::batteryStatusStr(char *buf, uint8_t bufLen) {
  // TP4056 CHRG pin: LOW = actively charging
  if (analogRead(PIN_TP_CHRG) < 200) {
    strlcpy(buf, "CHARGING", bufLen); return;
  }
  float v = batteryVoltage();
  if      (v > 4.1f) strlcpy(buf, "FULL",     bufLen);
  else if (v < 3.3f) strlcpy(buf, "LOW_BATT", bufLen);
  else               strlcpy(buf, "ON_BATT",  bufLen);
}
