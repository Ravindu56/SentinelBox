// =====================================================================
// main.cpp — ATmega328P / Mega2560 Main Controller
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
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
// _lastGsmRetry removed — GSM is on ESP32 node

// ── State ─────────────────────────────────────────────────────────────
static uint8_t _prevFlags   = 0xFF;
static bool    _gsmInitDone = false;
static char    _ts[20];
static char    _battSt[10];
static char    _flagTxt[64];

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

    // FIX: use if/else instead of ternary — DBGLN expands to nothing
    // when SERIAL_DEBUG=0, making ternary syntax invalid
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

    DBGLN(F("[GSM]  on ESP32 node — skipped"));
    DBGLN(F("[BOOT] Complete\n"));

    Power::initWatchdog();
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
            // FIX: if/else instead of ternary
            if (GSM::available()) { DBGLN(F("[GSM]  OK")); }
            else                  { DBGLN(F("[GSM]  FAIL — SMS disabled")); }
        }
    }

    // ── Async ticks ───────────────────────────────────────────────────
    Comms::tick();
    GSM::tick();
    Leds::tickAsync();

    unsigned long now = millis();

    // ── Sensor sampling + logging ─────────────────────────────────────
    if (now - _lastLog >= LOG_INTERVAL_MS) {
        _lastLog = now;
        Power::feedWatchdog();

        SensorData d;
        Sensors::read(d);
        uint8_t flags = Hazards::compute(d);

        if (flags != _prevFlags) {
            Leds::update(flags);
            if (flags) Leds::beepAsync(250, 2);
        }

        RtcTime::nowStr(_ts, sizeof(_ts));
        float battV = Power::batteryVoltage();
        Power::batteryStatusStr(_battSt, sizeof(_battSt));

        Storage::logRow(_ts, d, flags, battV, _battSt, Comms::lastGPS());
        Comms::sendTel(_ts, d, flags, battV, _battSt);

        if (flags != _prevFlags) {
            _prevFlags = flags;
            Hazards::flagsToStr(flags, _flagTxt, sizeof(_flagTxt));
            Comms::sendEvt(_ts, _flagTxt, Comms::lastGPS());

            // GSM SMS — handled by ESP32 node now
            // This block is intentionally a no-op (GSM::available() = false)
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
    }

    // ── Hourly heartbeat ──────────────────────────────────────────────
    if (now - _lastHeartbeat >= HEARTBEAT_MS) {
        _lastHeartbeat = now;
        Power::feedWatchdog();
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
        Storage::flush();
    }

    // ── SMS command handler (no-op — GSM on ESP32) ────────────────────
    if (_gsmInitDone) {
        const char* line = GSM::lastLine();
        if (line[0] != '\0') {
            if (strstr(line, "STATUS")) {
                SensorData d; Sensors::read(d);
                char reply[160];
                snprintf(reply, sizeof(reply),
                         "STATUS T:%.1f H:%.0f W:%d G:%d F:%d GPS:%s B:%.1fV",
                         (double)d.tempC, (double)d.humidity,
                         d.water, d.mq2, d.flame,
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

    // ── Sleep when all-clear ──────────────────────────────────────────
    if (_prevFlags == 0 && _gsmInitDone) {
        Power::enterSleep();
    }
}
