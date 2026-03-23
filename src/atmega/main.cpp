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

// ── Scheduler timestamps ──────────────────────────────────────────────
static unsigned long _lastLog       = 0;
static unsigned long _lastSms       = 0;
static unsigned long _lastHeartbeat = 0;

// ── State ─────────────────────────────────────────────────────────────
static uint8_t    _prevFlags    = 0xFF;   // 0xFF = "unset" sentinel
static uint8_t    _prevHazFlags = 0;      // last hazard value for rising-edge EVT
static bool       _gsmInitDone  = false;
static PowerMode  _prevMode     = PWR_MODE_MAINS;
static bool       _esp32Awake   = true;
static char       _ts[22];
static char       _battSt[12];
static char       _flagTxt[64];

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

    // Configure D2 (PIN_ESP_WAKE) — starts as input
    // ATmega outputs a pulse on this pin to wake ESP32 from deep sleep
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
            Power::wakeESP32();         // pulse D2 → ESP32 GPIO2 EXT0
            delay(1500);                // wait for ESP32 to boot
            Comms::sendWake();          // send WAKE frame over UART
            Comms::sendPwr("MAINS");    // notify power state
            _esp32Awake = true;
        }
        Leds::update(0);
        break;

    case PWR_MODE_BATT:
        DBGLN(F("[PWR]  → BATTERY_IDLE"));
        Comms::sendPwr("BATT");         // notify ESP32 before sleeping it
        delay(200);
        if (_esp32Awake) {
            Comms::sendSleep();         // ESP32 enters deep sleep
            delay(500);                 // give ESP32 time to sleep cleanly
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
        // Turn off all LEDs/buzzer to save power
        Leds::update(0);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────────
void loop() {
    Power::feedWatchdog();

    // ── GSM init block — stubbed (GSM on ESP32) ───────────────────────
    if (!_gsmInitDone) {
        if (millis() >= 5000UL) {
            _gsmInitDone = true;
            if (GSM::available()) { DBGLN(F("[GSM]  OK")); }
            else                  { DBGLN(F("[GSM]  FAIL — SMS on ESP32")); }
        }
    }

    // ── Async ticks ───────────────────────────────────────────────────
    Comms::tick();
    GSM::tick();
    Leds::tickAsync();

    unsigned long now = millis();

    // ── Read sensors + compute hazard flags ───────────────────────────
    SensorData d;
    Sensors::read(d);
    uint8_t flags = Hazards::compute(d);

    // ── Power state machine ───────────────────────────────────────────
    PowerMode mode = Power::updateMode(flags);

    if (mode != _prevMode) {
        _handleModeTransition(mode);
        _prevMode = mode;
    }

    // ─────────────────────────────────────────────────────────────────
    // Per-mode behaviour
    // ─────────────────────────────────────────────────────────────────
    switch (mode) {

    // ── MAINS_NORMAL or EMERGENCY: full active operation ─────────────
    case PWR_MODE_MAINS:
    case PWR_MODE_EMERG: {

        // Update LEDs on flag change
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

            // Log to SD
            Storage::logRow(_ts, d, flags, battV, _battSt, Comms::lastGPS());

            // Send telemetry to ESP32
            if (_esp32Awake) {
                Comms::sendTel(_ts, d, flags, battV, _battSt);
            }

            // Send EVT only on rising edge of a new hazard
            if (flags != 0 && flags != _prevHazFlags) {
                Hazards::flagsToStr(flags, _flagTxt, sizeof(_flagTxt));
                if (_esp32Awake) {
                    Comms::sendEvt(_ts, _flagTxt, Comms::lastGPS());
                }
                DBG(F("[EVT]  ")); DBGLN(_flagTxt);

                // Local GSM fallback (no-op in current wiring, GSM is on ESP32)
                if (_gsmInitDone && GSM::available() &&
                    Hazards::isCritical(flags) &&
                    (now - _lastSms) >= SMS_COOLDOWN_MS) {
                    char sms[160];
                    snprintf(sms, sizeof(sms),
                             "ALERT:%s T:%.1f H:%.0f GPS:%s B:%.1fV(%s) "
                             "MAP:maps.google.com/?q=%s",
                             _flagTxt,
                             isnan(d.tempC)    ? 0.0 : (double)d.tempC,
                             isnan(d.humidity) ? 0.0 : (double)d.humidity,
                             Comms::lastGPS(), (double)battV, _battSt,
                             Comms::lastGPS());
                    if (GSM::sendSMS(SMS_NUMBER_1, sms)) {
                        GSM::sendSMS(SMS_NUMBER_2, sms);
                        _lastSms = now;
                        DBGLN(F("[SMS]  Alert sent"));
                    }
                }
            }

            if (flags == 0) _prevHazFlags = 0;
            else            _prevHazFlags = flags;
            _prevFlags = flags;
        }

        // Hourly heartbeat
        if (now - _lastHeartbeat >= HEARTBEAT_MS) {
            _lastHeartbeat = now;
            Power::feedWatchdog();
            Storage::flush();

            if (_gsmInitDone && GSM::available()) {
                char hb[160];
                float bv = Power::batteryVoltage();
                Power::batteryStatusStr(_battSt, sizeof(_battSt));
                snprintf(hb, sizeof(hb),
                         "HB:%s GPS:%s B:%.1fV(%s) SD:%s",
                         _ts, Comms::lastGPS(), (double)bv, _battSt,
                         Storage::available() ? "OK" : "FAIL");
                GSM::sendSMS(SMS_NUMBER_1, hb);
            }
        }

        // SMS command handler (no-op when GSM is on ESP32)
        if (_gsmInitDone) {
            const char* line = GSM::lastLine();
            if (line[0] != '\0') {
                if (strstr(line, "STATUS")) {
                    SensorData ds; Sensors::read(ds);
                    char reply[160];
                    snprintf(reply, sizeof(reply),
                             "STATUS T:%.1f H:%.0f W:%d G:%d F:%d GPS:%s B:%.1fV",
                             (double)ds.tempC, (double)ds.humidity,
                             ds.water, ds.mq2, ds.flame,
                             Comms::lastGPS(), (double)Power::batteryVoltage());
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

        break;  // end PWR_MODE_MAINS / PWR_MODE_EMERG
    }

    // ── BATTERY_IDLE: log once, then sleep 30s ────────────────────────
    case PWR_MODE_BATT: {

        // Update flags state without LED (LEDs off on battery idle)
        _prevFlags    = flags;
        _prevHazFlags = flags;

        RtcTime::nowStr(_ts, sizeof(_ts));
        float battV = Power::batteryVoltage();
        Power::batteryStatusStr(_battSt, sizeof(_battSt));

        // Log reduced-frequency row to SD
        Storage::logRow(_ts, d, flags, battV, _battSt, "0,0");
        Storage::flush();

        DBG(F("[PWR]  Sleeping 30s | batt=")); DBGLN(battV);

        // Sleep 30s — INT0 on D2 can wake early if ESP32 signals
        Power::sleepMs(BATT_IDLE_SLEEP_MS, true);

        // After waking, re-check if a hazard appeared mid-sleep
        SensorData dPost; Sensors::read(dPost);
        uint8_t flagsPost = Hazards::compute(dPost);
        if (flagsPost != 0) {
            // Hazard detected during sleep — next loop() will trigger EMERGENCY mode
            _prevFlags = 0xFF;   // force flag change detection on next cycle
        }

        break;
    }

    // ── CRITICAL_BATT: log once, sleep 5 minutes ─────────────────────
    case PWR_MODE_CRIT: {

        _prevFlags    = flags;
        _prevHazFlags = flags;

        RtcTime::nowStr(_ts, sizeof(_ts));
        float battV = Power::batteryVoltage();
        Power::batteryStatusStr(_battSt, sizeof(_battSt));

        Storage::logRow(_ts, d, flags, battV, _battSt, "0,0");
        Storage::flush();

        DBG(F("[PWR]  CRIT sleep 5min | batt=")); DBGLN(battV);

        // Sleep 5 minutes; no INT0 wake (conserve power)
        Power::sleepMs(BATT_CRIT_SLEEP_MS, false);

        break;
    }

    }  // end switch(mode)

    // Feed WDT at end of every active loop pass
    // (sleepMs() handles WDT internally during sleep)
    Power::feedWatchdog();
}
