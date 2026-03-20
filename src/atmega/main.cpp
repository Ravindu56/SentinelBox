// =====================================================================
// main.cpp — ATmega328P / Mega2560 Main Controller
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
//
// Compiles for BOTH Mega (testing) and ATmega328P (production):
//   pio run -e mega          → Mega2560
//   pio run -e atmega328p    → Bare ATmega328P chip
//
// Modules:
//   sensors   — DHT22, water, MQ-2, flame, vibration, panic
//   hazards   — bitmask detection + text conversion
//   rtc_time  — DS3231 timestamps
//   storage   — Micro SD CSV logging (no EEPROM)
//   gsm       — SIM800L SMS + heartbeat
//   leds      — RGB LED + buzzer state machine
//   comms     — ATmega↔ESP32 serial protocol
//   power     — battery ADC, TP4056, watchdog, sleep
//
// millis()-based scheduling — NO blocking delay() in main loop.
// PROGMEM / F() macros used throughout to minimise SRAM usage.
// =====================================================================

#include <Arduino.h>
#include <Wire.h>
#include <avr/wdt.h>
#include "../../include/config.h"

#include "sensors.h"
#include "hazards.h"
#include "rtc_time.h"
#include "storage.h"
#include "gsm.h"
#include "leds.h"
#include "comms.h"
#include "power.h"

// ── millis()-based scheduler timestamps ──────────────────────────────
static unsigned long _lastLog       = 0;
static unsigned long _lastSms       = 0;
static unsigned long _lastHeartbeat = 0;

// ── State ────────────────────────────────────────────────────────────
static uint8_t _prevFlags  = 0xFF;  // force first-run LED update
static char    _ts[20];             // "YYYY-MM-DD HH:MM:SS"
static char    _battSt[10];
static char    _flagTxt[64];

// ── Debug print helper (compiled out when SERIAL_DEBUG=0) ────────────
#if SERIAL_DEBUG
  #define DBG(x)    Serial.print(x)
  #define DBGLN(x)  Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ─────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────
void setup() {
  wdt_disable();     // disable watchdog first — prevent reset loops

  Wire.begin();
  Leds::init();      // green LED on immediately

  // Serial for ESP32 link (and USB debug on Mega)
  Comms::init();

  DBGLN(F("\n============================================="));
  DBGLN(F("  Disaster BlackBox — booting"));
  DBGLN(F("============================================="));

  // RTC
  if (!RtcTime::init()) {
    DBGLN(F("[RTC]  FAIL — check A4/A5"));
  } else {
    DBGLN(F("[RTC]  OK"));
  }

  // Sensors
  Sensors::init();
  DBGLN(F("[SENS] OK"));

  // SD card
  if (!Storage::init()) {
    DBGLN(F("[SD]   FAIL — logging disabled"));
    Leds::beep(100); delay(100); Leds::beep(100); // 2 short beeps = SD fail
  } else {
    DBGLN(F("[SD]   OK — log.csv ready"));
  }

  // GSM — auto-detects baud
  GSM::init();
  if (GSM::available()) {
    DBGLN(F("[GSM]  OK"));
  } else {
    DBGLN(F("[GSM]  FAIL — SMS disabled"));
  }

  // Boot complete
  Leds::beep(200);
  DBGLN(F("[BOOT] Complete\n"));

  Power::initWatchdog();
}

// ─────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────
void loop() {
  Power::feedWatchdog();

  // ── Non-blocking async tasks (every loop iteration) ────────────────
  Comms::tick();     // receive GPS from ESP32
  GSM::tick();       // receive incoming SMS lines
  Leds::tickAsync(); // non-blocking buzzer beeping

  unsigned long now = millis();

  // ── Sensor sampling + logging (every LOG_INTERVAL_MS) ──────────────
  if (now - _lastLog >= LOG_INTERVAL_MS) {
    _lastLog = now;

    // 1. Read sensors
    SensorData d;
    Sensors::read(d);

    // 2. Compute hazards
    uint8_t flags = Hazards::compute(d);

    // 3. Update LED (only when flags change → saves CPU + flicker)
    if (flags != _prevFlags) {
      Leds::update(flags);
      if (flags != 0) Leds::beepAsync(250, 2);
    }

    // 4. Timestamps + battery
    RtcTime::nowStr(_ts, sizeof(_ts));
    float battV = Power::batteryVoltage();
    Power::batteryStatusStr(_battSt, sizeof(_battSt));

    // 5. SD logging
    Storage::logRow(_ts, d, flags, battV, _battSt, Comms::lastGPS());

    // 6. Send telemetry to ESP32
    Comms::sendTel(_ts, d, flags, battV, _battSt);

    // 7. Hazard state change handling
    if (flags != _prevFlags) {
      _prevFlags = flags;
      Hazards::flagsToStr(flags, _flagTxt, sizeof(_flagTxt));

      // Send event to ESP32
      Comms::sendEvt(_ts, _flagTxt, Comms::lastGPS());

      // SMS alert (critical hazards only, with cooldown)
      if (Hazards::isCritical(flags) &&
          (now - _lastSms) >= SMS_COOLDOWN_MS) {
        if (GSM::available()) {
          // Build SMS — char array to avoid heap fragmentation
          char sms[160];
          snprintf(sms, sizeof(sms),
                   "ALERT:%s T:%.1f H:%.0f "
                   "GPS:%s B:%.1fV(%s) "
                   "MAP:maps.google.com/?q=%s",
                   _flagTxt,
                   isnan(d.tempC)    ? 0.0f : d.tempC,
                   isnan(d.humidity) ? 0.0f : d.humidity,
                   Comms::lastGPS(),
                   battV, _battSt,
                   Comms::lastGPS());
          if (GSM::sendSMS(SMS_NUMBER_1, sms)) {
            GSM::sendSMS(SMS_NUMBER_2, sms);
            _lastSms = now;
            DBGLN(F("[SMS]  Alert sent"));
          }
        }
      }
    }

    // Debug serial output (compiled out in production)
#if SERIAL_DEBUG
    Serial.print(F("[DATA] T="));   Serial.print(d.tempC, 1);
    Serial.print(F(" H="));        Serial.print(d.humidity, 1);
    Serial.print(F(" W="));        Serial.print(d.water);
    Serial.print(F(" G="));        Serial.print(d.mq2);
    Serial.print(F(" F="));        Serial.print(d.flame);
    Serial.print(F(" VIB="));      Serial.print(d.vib);
    Serial.print(F(" FLG=0x"));   Serial.print(flags, HEX);
    Serial.print(F(" GPS="));      Serial.print(Comms::lastGPS());
    Serial.print(F(" B="));        Serial.print(battV, 2);
    Serial.print(F("V "));         Serial.println(_battSt);
#endif
  }

  // ── Hourly heartbeat SMS ───────────────────────────────────────────
  if (now - _lastHeartbeat >= HEARTBEAT_MS) {
    _lastHeartbeat = now;
    if (GSM::available()) {
      char hb[160];
      float bv = Power::batteryVoltage();
      Power::batteryStatusStr(_battSt, sizeof(_battSt));
      snprintf(hb, sizeof(hb),
               "HB:%s GPS:%s B:%.1fV(%s) SD:%s",
               _ts,
               Comms::lastGPS(),
               bv, _battSt,
               Storage::available() ? "OK" : "FAIL");
      GSM::sendSMS(SMS_NUMBER_1, hb);
    }
    Storage::flush(); // force flush on heartbeat
  }

  // ── Incoming SMS command handler ───────────────────────────────────
  {
    const char *line = GSM::lastLine();
    if (line[0] != '\0') {
      if (strstr(line, "STATUS")) {
        // Reply with current readings
        SensorData d; Sensors::read(d);
        char reply[160];
        snprintf(reply, sizeof(reply),
                 "STATUS T:%.1f H:%.0f W:%d G:%d F:%d GPS:%s B:%.1fV",
                 d.tempC, d.humidity, d.water, d.mq2, d.flame,
                 Comms::lastGPS(),
                 Power::batteryVoltage());
        GSM::sendSMS(SMS_NUMBER_1, reply);
      } else if (strstr(line, "LOCATION")) {
        char reply[100];
        snprintf(reply, sizeof(reply),
                 "LOC:%s maps.google.com/?q=%s",
                 Comms::lastGPS(), Comms::lastGPS());
        GSM::sendSMS(SMS_NUMBER_1, reply);
      }
      GSM::clearLastLine();
    }
  }

  // ── Power-Down sleep (only when all clear) ─────────────────────────
  // Wakes on vibration INT0 or watchdog overflow
  if (_prevFlags == 0) {
    Power::enterSleep();
  }
}
