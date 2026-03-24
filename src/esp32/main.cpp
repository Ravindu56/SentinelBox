// =====================================================================
// main.cpp — ESP32 WROOM Utility Node
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
//
// Modules:
//   gps_parser  — NEO-6M GPS on UART2 (GPIO16 RX / GPIO17 TX)
//   uno_link    — ATmega link on UART1 (GPIO4 RX / GPIO5 TX)
//   gsm_node    — SIM800L GSM on UART0 (GPIO26 RX / GPIO27 TX)
//   wifi_mqtt   — WiFi + MQTT with auto-reconnect + AP fallback
//   web_server  — Async dashboard + SSE live updates
//
// Power coordination:
//   GPIO2 (EXT0) ← ATmega D2: HIGH pulse wakes ESP32 from deep sleep
//   UART1 frames: SLEEP → deep sleep | WAKE → acknowledge | PWR,x → state
//
// millis()-based scheduling — NO blocking delay() in main loop.
// =====================================================================

#include <Arduino.h>
#include <ArduinoOTA.h>
#include "esp_sleep.h"
#include "../../include/config.h"

#include "gps_parser.h"
#include "uno_link.h"
#include "gsm_node.h"
#include "wifi_mqtt.h"
#include "web_server.h"

// ── GPIO2: ATmega D2 → EXT0 wake source ──────────────────────────────
#define PIN_WAKE_FROM_ATMEGA  2

// ── Scheduler ─────────────────────────────────────────────────────────
static unsigned long _lastDashPush  = 0;
static unsigned long _lastHeartbeat = 0;
static unsigned long _lastSmsSent   = 0;
static unsigned long _lastGpsSent   = 0;
static bool          _gsmInitDone   = false;

// ── Power state ───────────────────────────────────────────────────────
// Tracks last known power mode reported by ATmega
// "MAINS" | "BATT" | "CRIT"
static char _pwrState[8] = "MAINS";

// ── Forward declarations ──────────────────────────────────────────────
static void _handlePwrFrame(const char *state);
static void _sendTimeToAtmega();

// ── MQTT incoming command callback ────────────────────────────────────
void onMqttMessage(char *topic, byte *payload, unsigned int len) {
    char cmd[32];
    uint8_t l = min((unsigned int)31, len);
    strncpy(cmd, (char *)payload, l);
    cmd[l] = '\0';

    const TelData &t = UnoLink::telemetry();
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
    char reply[160];

    if (strstr(cmd, "STATUS")) {
        snprintf(reply, sizeof(reply),
                 "T:%.1f H:%.0f W:%d G:%d GPS:%s B:%.1fV(%s) PWR:%s",
                 t.tempC, t.humidity, t.water, t.mq2,
                 gps, t.battV, t.battSt, _pwrState);
        WifiMqtt::publish(MQTT_TOPIC_EVT, reply);
        if (GsmNode::available())
            GsmNode::sendSMS(SMS_NUMBER_1, reply);

    } else if (strstr(cmd, "LOCATION")) {
        snprintf(reply, sizeof(reply),
                 "GPS:%s maps.google.com/?q=%s", gps, gps);
        WifiMqtt::publish(MQTT_TOPIC_EVT, reply);
        if (GsmNode::available())
            GsmNode::sendSMS(SMS_NUMBER_1, reply);
    }
}

// ─────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // ── Print wake reason ──────────────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println(F("[PWR]  Woke from ATmega GPIO pulse (EXT0)"));
    } else if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        Serial.println(F("[BOOT] Cold boot / reset"));
    }

    // ── Configure EXT0 wakeup on GPIO2 HIGH ───────────────────────
    // GPIO2 must have a pull-down so it doesn't float during sleep
    pinMode(PIN_WAKE_FROM_ATMEGA, INPUT);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WAKE_FROM_ATMEGA, 1);

    Serial.println(F("\n[ESP32] BlackBox utility node booting..."));

    GpsParser::init();    // UART2: GPIO16/17 — NEO-6M GPS @ 9600
    UnoLink::init();      // UART1: GPIO4/5   — ATmega link @ 115200
    WifiMqtt::init();
    WifiMqtt::setCallback(onMqttMessage);
    WebDash::init();

    // OTA firmware updates over WiFi
    ArduinoOTA.setHostname("sentinelbox-esp32");
    ArduinoOTA.onStart([]()  { Serial.println(F("[OTA] Start")); });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Error %u\n", e);
    });
    ArduinoOTA.begin();

    Serial.print(F("[WiFi] IP: "));
    Serial.println(WifiMqtt::localIP());
    Serial.println(F("[ESP32] Ready\n"));
}

// ─────────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────────
void loop() {
    // ── Deferred GSM init (3s after boot) ─────────────────────────
    if (!_gsmInitDone && millis() >= 3000UL) {
        _gsmInitDone = true;
        GsmNode::init();
        Serial.printf("[GSM]  %s\n",
            GsmNode::available() ? "Ready" : "Not connected — SMS disabled");
    }

    // ── Non-blocking module ticks ──────────────────────────────────
    GpsParser::tick();
    UnoLink::tick();      // also parses SLEEP/WAKE/PWR frames now
    GsmNode::tick();
    WifiMqtt::tick();
    ArduinoOTA.handle();

    unsigned long now = millis();
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));

    // ── Power coordination frames from ATmega ─────────────────────
    if (UnoLink::sleepRequested()) {
        UnoLink::clearSleepRequest();
        Serial.println(F("[PWR]  ATmega requested deep sleep — entering now"));
        Serial.flush();
        if (WifiMqtt::mqttConnected()) {
            WifiMqtt::publish("blackbox/power",
                              "{\"pwr\":\"SLEEPING\"}");
            delay(100);
        }
        esp_deep_sleep_start();
    }

    const char *pwrFrame = UnoLink::lastPwrState();
    if (pwrFrame[0] != '\0') {
        _handlePwrFrame(pwrFrame);
        UnoLink::clearPwrState();
    }

    // ── Send GPS time to ATmega when GPS lock is fresh ─────────────
    if (GpsParser::hasTime() && (now - _lastGpsSent >= 60000UL)) {
        _lastGpsSent = now;
        _sendTimeToAtmega();
    }

    // ── New telemetry frame from ATmega ───────────────────────────
    const TelData &tel = UnoLink::telemetry();
    if (tel.fresh) {
        // ── Build telemetry JSON — includes panic field ────────────
        char json[300];
        snprintf(json, sizeof(json),
            "{\"ts\":\"%s\",\"t\":%.1f,\"h\":%.0f,"
            "\"w\":%d,\"g\":%d,\"f\":%d,\"v\":%d,"
            "\"panic\":%d,"
            "\"flags\":%u,\"bv\":%.2f,\"bs\":\"%s\","
            "\"gps\":\"%s\",\"sats\":%u,\"pwr\":\"%s\"}",
            tel.ts, tel.tempC, tel.humidity,
            tel.water, tel.mq2, tel.flame, tel.vib,
            tel.panic,
            tel.flags, tel.battV, tel.battSt,
            gps, GpsParser::sats(), _pwrState);
        WifiMqtt::publish(MQTT_TOPIC_TEL, json);

        // ACK back to ATmega
        UnoLink::sendAck();
    }

    // ── Hazard event from ATmega ──────────────────────────────────
    const char *evt = UnoLink::lastEvent();
    if (evt[0] != '\0') {
        char payload[160];
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"%s\",\"gps\":\"%s\",\"pwr\":\"%s\"}",
                 evt, gps, _pwrState);
        WifiMqtt::publish(MQTT_TOPIC_EVT, payload);

        bool isCritical   = (strstr(evt, "NORMAL") == nullptr);
        bool isPanic      = (strstr(evt, "PANIC")  != nullptr);
        bool cooldownOk   = (now - _lastSmsSent) >= SMS_COOLDOWN_MS;

        // ── PANIC bypasses the SMS cooldown — always send immediately
        if (GsmNode::available() && isCritical && (cooldownOk || isPanic)) {
            char sms[160];
            snprintf(sms, sizeof(sms),
                     "%sALERT:%s T:%.1fC H:%.0f%% GPS:%s B:%.1fV "
                     "MAP:maps.google.com/?q=%s",
                     isPanic ? "[PANIC] " : "",
                     evt, tel.tempC, tel.humidity,
                     gps, tel.battV, gps);
            if (GsmNode::sendSMS(SMS_NUMBER_1, sms)) {
                GsmNode::sendSMS(SMS_NUMBER_2, sms);
                // Only update cooldown timer for non-panic events
                // so repeated panic presses still send each time
                if (!isPanic) _lastSmsSent = now;
                Serial.println(isPanic
                    ? F("[GSM] PANIC SMS sent to both numbers")
                    : F("[GSM] Alert SMS sent to both numbers"));
            }
        }

        UnoLink::clearEvent();
    }

    // ── Incoming SMS command handler ──────────────────────────────
    const char *gsmLine = GsmNode::lastLine();
    if (gsmLine[0] != '\0') {
        if (strstr(gsmLine, "STATUS")) {
            char reply[160];
            snprintf(reply, sizeof(reply),
                     "STATUS T:%.1f H:%.0f W:%d G:%d F:%d GPS:%s B:%.1fV(%s) PWR:%s",
                     tel.tempC, tel.humidity, tel.water,
                     tel.mq2, tel.flame, gps, tel.battV, tel.battSt,
                     _pwrState);
            GsmNode::sendSMS(SMS_NUMBER_1, reply);

        } else if (strstr(gsmLine, "LOCATION")) {
            char reply[100];
            snprintf(reply, sizeof(reply),
                     "LOC:%s maps.google.com/?q=%s", gps, gps);
            GsmNode::sendSMS(SMS_NUMBER_1, reply);

        } else if (strstr(gsmLine, "BATT")) {
            char reply[80];
            snprintf(reply, sizeof(reply),
                     "BATTERY %.2fV (%s) Mode:%s",
                     tel.battV, tel.battSt, _pwrState);
            GsmNode::sendSMS(SMS_NUMBER_1, reply);
        }
        GsmNode::clearLastLine();
    }

    // ── Web dashboard SSE push every 2s ───────────────────────────
    if (now - _lastDashPush >= 2000UL) {
        _lastDashPush = now;
        WebDash::pushUpdate(tel, gps, GpsParser::sats());
    }

    // ── Hourly heartbeat SMS ──────────────────────────────────────
    if (now - _lastHeartbeat >= HEARTBEAT_MS) {
        _lastHeartbeat = now;
        if (GsmNode::available()) {
            char hb[160];
            snprintf(hb, sizeof(hb),
                     "HB:%s GPS:%s B:%.1fV(%s) PWR:%s WiFi:%s MQTT:%s",
                     tel.ts, gps, tel.battV, tel.battSt, _pwrState,
                     WifiMqtt::wifiConnected() ? "OK" : "FAIL",
                     WifiMqtt::mqttConnected() ? "OK" : "FAIL");
            GsmNode::sendSMS(SMS_NUMBER_1, hb);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// _handlePwrFrame — act on PWR,x from ATmega
// ─────────────────────────────────────────────────────────────────────
static void _handlePwrFrame(const char *state) {
    strlcpy(_pwrState, state, sizeof(_pwrState));
    Serial.print(F("[PWR]  State from ATmega: ")); Serial.println(state);

    char payload[48];
    snprintf(payload, sizeof(payload), "{\"pwr\":\"%s\"}", state);
    if (WifiMqtt::mqttConnected()) {
        WifiMqtt::publish("blackbox/power", payload);
    }

    if (strcmp(state, "CRIT") == 0 && GsmNode::available()) {
        const TelData &t = UnoLink::telemetry();
        char sms[120];
        snprintf(sms, sizeof(sms),
                 "WARNING: SentinelBox battery critical (%.2fV). "
                 "Device entering long-sleep mode.",
                 t.battV);
        GsmNode::sendSMS(SMS_NUMBER_1, sms);
    }
}

// ─────────────────────────────────────────────────────────────────────
// _sendTimeToAtmega — send GPS time string for DS3231 sync
// Format: TIME,YYYY-MM-DDTHH:MM:SS
// ─────────────────────────────────────────────────────────────────────
static void _sendTimeToAtmega() {
    char timeBuf[24];
    GpsParser::timeStr(timeBuf, sizeof(timeBuf));
    if (timeBuf[0] == '\0') return;
    char frame[32];
    snprintf(frame, sizeof(frame), "TIME,%s", timeBuf);
    UnoLink::sendRaw(frame);
    Serial.print(F("[GPS]  Time synced to ATmega: ")); Serial.println(timeBuf);
}
