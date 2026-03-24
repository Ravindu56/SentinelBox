// =====================================================================
// main.cpp — ATmega328P / Mega2560 Main Controller
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
// Power State Machine: MAINS_NORMAL / BATTERY_IDLE / EMERGENCY / CRITICAL_BATT
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

// ── Scheduler timestamps ────────────────────────────────────────────
static unsigned long _lastLog       = 0;
// static unsigned long _lastSms       = 0;
static unsigned long _lastHeartbeat = 0;

// ── State ─────────────────────────────────────────────────────────────────
static uint8_t    _prevFlags    = 0xFF;   // 0xFF = "unset" sentinel
static uint8_t    _prevHazFlags = 0;
static bool       _gsmInitDone  = false;
static PowerMode  _prevMode     = PWR_MODE_MAINS;
static bool       _esp32Awake   = true;

// ── Shared string buffers (static = live in .bss, not stack) ───────────────
// Sized to exact worst-case content + null terminator:
//   _ts      : "YYYY-MM-DD HH:MM:SS" = 19 chars + '\0' = 20
//   _battSt  : "LOW_BATT"            =  8 chars + '\0' = 10 (rounded)
//   _flagTxt : all flags worst case   = ~38 chars + '\0' = 48
static char _ts[20];
static char _battSt[10];
static char _flagTxt[48];

#if SERIAL_DEBUG
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ─────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────
void setup() {
    wdt_disable();

    Wire.begin();
    Leds::init();
    Comms::init();

    DBGLN(F("\n============================================="));
    DBGLN(F("  Disaster BlackBox — booting"));
    DBGLN(F("============================================="));

    if (RtcTime::init()) { DBGLN(F("[RTC]  OK")); }
    else                  { DBGLN(F("[RTC]  FAIL")); }

    Sensors::init();
    DBGLN(F("[SENS] OK"));

    if (!Storage::init()) {
        DBGLN(F("[SD]   FAIL"));
        Leds::beep(100); delay(100); Leds::beep(100);
    } else {
        DBGLN(F("[SD]   OK"));
    }

    pinMode(PIN_ESP_WAKE, INPUT);

    DBGLN(F("[GSM]  on ESP32 node — skipped"));
    DBGLN(F("[PWR]  State machine ready"));
    DBGLN(F("[BOOT] Complete\n"));

    Power::initWatchdog();
}

// ─────────────────────────────────────────────────────────────────────
// _handleModeTransition — called ONCE when mode changes
// ─────────────────────────────────────────────────────────────────────
static void _handleModeTransition(PowerMode newMode) {
    switch (newMode) {

    case PWR_MODE_MAINS:
        DBGLN(F("[PWR]  → MAINS_NORMAL"));
        if (!_esp32Awake) {
            Power::wakeESP32();
            delay(1500);
            Comms::sendWake();
            Comms::sendPwr("MAINS");
            _esp32Awake = true;
        }
        Leds::update(0);
        break;

    case PWR_MODE_BATT:
        DBGLN(F("[PWR]  → BATTERY_IDLE"));
        Comms::sendPwr("BATT");
        delay(200);
        if (_esp32Awake) {
            Comms::sendSleep();
            delay(500);
            _esp32Awake = false;
        }
        Leds::update(0);
        break;

    case PWR_MODE_EMERG:
        DBGLN(F("[PWR]  → EMERGENCY"));
        if (!_esp32Awake) {
            Power::wakeESP32();
            delay(1500);
            Comms::sendWake();
            _esp32Awake = true;
        }
        break;

    case PWR_MODE_CRIT:
        DBGLN(F("[PWR]  → CRITICAL_BATT"));
        if (_esp32Awake) {
            Comms::sendPwr("CRIT");
            delay(200);
            Comms::sendSleep();
            delay(500);
            _esp32Awake = false;
        }
        Leds::update(0);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────────
void loop() {
    Power::feedWatchdog();

    // ── GSM stub check (GSM is on ESP32, always returns false) ─────────────
    if (!_gsmInitDone && millis() >= 5000UL) {
        _gsmInitDone = true;
    }

    // ── Async ticks ─────────────────────────────────────────────────────────
    Comms::tick();
    Leds::tickAsync();

    unsigned long now = millis();

    // ── Read sensors + compute hazard flags ───────────────────────────
    SensorData d;
    Sensors::read(d);
    uint8_t flags = Hazards::compute(d);

    // ── Power state machine ────────────────────────────────────────────────
    PowerMode mode = Power::updateMode(flags);

    if (mode != _prevMode) {
        _handleModeTransition(mode);
        _prevMode = mode;
    }

    switch (mode) {

    // ── MAINS_NORMAL or EMERGENCY: full active operation ───────────────
    case PWR_MODE_MAINS:
    case PWR_MODE_EMERG: {

        if (flags != _prevFlags) {
            Leds::update(flags);
            if (flags) Leds::beepAsync(250, 2);
        }

        if (now - _lastLog >= LOG_INTERVAL_MS) {
            _lastLog = now;
            Power::feedWatchdog();

            RtcTime::nowStr(_ts, sizeof(_ts));
            float battV = Power::batteryVoltage();
            Power::batteryStatusStr(_battSt, sizeof(_battSt));

            Storage::logRow(_ts, d, flags, battV, _battSt, Comms::lastGPS());

            if (_esp32Awake) {
                Comms::sendTel(_ts, d, flags, battV, _battSt);
            }

            // Rising-edge EVT on new hazard
            if (flags != 0 && flags != _prevHazFlags) {
                Hazards::flagsToStr(flags, _flagTxt, sizeof(_flagTxt));
                if (_esp32Awake) {
                    Comms::sendEvt(_ts, _flagTxt, Comms::lastGPS());
                }
                DBG(F("[EVT]  ")); DBGLN(_flagTxt);
                // NOTE: SMS is handled by ESP32; GSM stub always returns false
                // Dead-code GSM SMS block removed to save ~480 bytes stack RAM
            }

            _prevHazFlags = (flags == 0) ? 0 : flags;
            _prevFlags    = flags;
        }

        // Hourly heartbeat — SD flush only (GSM stub removed)
        if (now - _lastHeartbeat >= HEARTBEAT_MS) {
            _lastHeartbeat = now;
            Power::feedWatchdog();
            Storage::flush();
            DBGLN(F("[HB]   SD flush"));
        }

        break;
    }

    // ── BATTERY_IDLE: log once, then sleep 30s ────────────────────────
    case PWR_MODE_BATT: {

        _prevFlags    = flags;
        _prevHazFlags = flags;

        RtcTime::nowStr(_ts, sizeof(_ts));
        float battV = Power::batteryVoltage();
        Power::batteryStatusStr(_battSt, sizeof(_battSt));

Storage::logRow(_ts, d, flags, battV, _battSt, "0,0");
        Storage::flush();

        DBG(F("[PWR]  Sleeping 30s batt=")); DBGLN(battV);

        Power::sleepMs(BATT_IDLE_SLEEP_MS, true);

        SensorData dPost; Sensors::read(dPost);
        uint8_t flagsPost = Hazards::compute(dPost);
        if (flagsPost != 0) {
            _prevFlags = 0xFF;
        }
        (void)flagsPost;

        break;
    }

    // ── CRITICAL_BATT: log once, sleep 5 minutes ──────────────────────
    case PWR_MODE_CRIT: {

        _prevFlags    = flags;
        _prevHazFlags = flags;

        RtcTime::nowStr(_ts, sizeof(_ts));
        float battV = Power::batteryVoltage();
        Power::batteryStatusStr(_battSt, sizeof(_battSt));

        Storage::logRow(_ts, d, flags, battV, _battSt, "0,0");
        Storage::flush();

        DBG(F("[PWR]  CRIT sleep 5min batt=")); DBGLN(battV);

        Power::sleepMs(BATT_CRIT_SLEEP_MS, false);

        break;
    }

    }  // end switch(mode)

    Power::feedWatchdog();
}
