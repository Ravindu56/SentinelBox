// =====================================================================
// main.cpp — ESP32 WROOM Utility Node
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
//
// UART allocation:
//   UART0 (Serial)  — USB debug monitor        GPIO1 TX / GPIO3 RX
//   UART1 (Serial1) — SIM800L GSM              GPIO14 TX / GPIO12 RX
//   UART2 (Serial2) — NEO-6M GPS              GPIO17 TX / GPIO16 RX
//   UnoLink         — ATmega link (Serial2 alt) GPIO5 TX / GPIO4 RX
//
// Power coordination:
//   GPIO2 (EXT0) ← ATmega D2: HIGH pulse wakes ESP32 from deep sleep
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

#define PIN_WAKE_FROM_ATMEGA  2

// ── Scheduler ─────────────────────────────────────────────────────
static unsigned long _lastDashPush  = 0;
static unsigned long _lastHeartbeat = 0;
static unsigned long _lastSmsSent   = 0;
static unsigned long _lastGpsSent   = 0;
static unsigned long _lastGpsReport = 0;   // 1-min GPS location report
static bool          _gsmInitDone   = false;

// ── Power state ───────────────────────────────────────────────────
static char _pwrState[8] = "MAINS";

// ── Forward declarations ──────────────────────────────────────────────
static void _handlePwrFrame(const char *state);
static void _sendTimeToAtmega();
static void _reportGpsLocation();

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

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT0)
        Serial.println(F("[PWR]  Woke from ATmega GPIO pulse (EXT0)"));
    else
        Serial.println(F("[BOOT] Cold boot / reset"));

    pinMode(PIN_WAKE_FROM_ATMEGA, INPUT);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WAKE_FROM_ATMEGA, 1);

    Serial.println(F("\n[ESP32] BlackBox utility node booting..."));

    GpsParser::init();    // UART2: GPIO16 RX / GPIO17 TX — NEO-6M @ 9600
    UnoLink::init();      // UART using GPIO4 RX / GPIO5 TX — ATmega @ 115200
    WifiMqtt::init();
    WifiMqtt::setCallback(onMqttMessage);
    WebDash::init();

    ArduinoOTA.setHostname("sentinelbox-esp32");
    ArduinoOTA.onStart([]()  { Serial.println(F("[OTA]  Start")); });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA]  Error %u\n", e);
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
    // ── Deferred GSM init — 3s after boot so Serial is stable ────────
    if (!_gsmInitDone && millis() >= 3000UL) {
        _gsmInitDone = true;
        GsmNode::init();   // UART1: GPIO12 RX / GPIO14 TX — SIM800L @ 9600
        Serial.printf("[GSM]  %s\n",
            GsmNode::available() ? "SIM800L ready" : "Not found — SMS disabled");
    }

    // ── Non-blocking module ticks ────────────────────────────────
    GpsParser::tick();
    UnoLink::tick();
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
            WifiMqtt::publish("blackbox/power", "{\"pwr\":\"SLEEPING\"}");
            delay(100);
        }
        esp_deep_sleep_start();
    }

    const char *pwrFrame = UnoLink::lastPwrState();
    if (pwrFrame[0] != '\0') {
        _handlePwrFrame(pwrFrame);
        UnoLink::clearPwrState();
    }

    // ── Send GPS time to ATmega every 60s when locked ─────────────
    if (GpsParser::hasTime() && (now - _lastGpsSent >= 60000UL)) {
        _lastGpsSent = now;
        _sendTimeToAtmega();
    }

    // ── 1-minute GPS location report → Serial + SMS both numbers ────
    if (now - _lastGpsReport >= 60000UL) {
        _lastGpsReport = now;
        _reportGpsLocation();
    }

    // ── New telemetry frame from ATmega ──────────────────────────
    const TelData &tel = UnoLink::telemetry();
    if (tel.fresh) {
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
        UnoLink::sendAck();
    }

    // ── Hazard event from ATmega ──────────────────────────────
    const char *evt = UnoLink::lastEvent();
    if (evt[0] != '\0') {
        char payload[160];
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"%s\",\"gps\":\"%s\",\"pwr\":\"%s\"}",
                 evt, gps, _pwrState);
        WifiMqtt::publish(MQTT_TOPIC_EVT, payload);

        bool isCritical = (strstr(evt, "NORMAL") == nullptr);
        bool isPanic    = (strstr(evt, "PANIC")  != nullptr);
        bool cooldownOk = (now - _lastSmsSent) >= SMS_COOLDOWN_MS;

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
                if (!isPanic) _lastSmsSent = now;
                Serial.println(isPanic
                    ? F("[GSM]  PANIC SMS sent to both numbers")
                    : F("[GSM]  Alert SMS sent to both numbers"));
            }
        }
        UnoLink::clearEvent();
    }

    // ── Incoming SMS command handler ───────────────────────────
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

    // ── Web dashboard SSE push every 2s ─────────────────────────
    if (now - _lastDashPush >= 2000UL) {
        _lastDashPush = now;
        WebDash::pushUpdate(tel, gps, GpsParser::sats());
    }

    // ── Hourly heartbeat SMS ───────────────────────────────────
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
// _reportGpsLocation — every 60s: print to Serial + SMS both numbers
// ─────────────────────────────────────────────────────────────────────
static void _reportGpsLocation() {
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
    uint8_t sats = GpsParser::sats();
    bool    lock = GpsParser::valid();

    Serial.println(F("─────────────────────────────────────"));
    Serial.print(F("[GPS]  Location report @ "));
    Serial.print(millis() / 1000); Serial.println(F("s uptime"));
    if (lock) {
        Serial.print(F("[GPS]  Coords : ")); Serial.println(gps);
        Serial.print(F("[GPS]  Sats   : ")); Serial.println(sats);
        Serial.print(F("[GPS]  Map    : https://maps.google.com/?q="));
        Serial.println(gps);
    } else {
        Serial.println(F("[GPS]  No fix — waiting for satellite lock"));
        Serial.print(F("[GPS]  Sats visible: ")); Serial.println(sats);
    }
    Serial.println(F("─────────────────────────────────────"));

    if (!GsmNode::available()) {
        Serial.println(F("[GPS]  SMS skipped — GSM not available"));
        return;
    }
    if (!lock) {
        Serial.println(F("[GPS]  SMS skipped — no GPS fix yet"));
        return;
    }

    char sms[160];
    snprintf(sms, sizeof(sms),
             "SentinelBox Location:\n"
             "GPS: %s\n"
             "Map: maps.google.com/?q=%s\n"
             "Sats: %u  PWR: %s",
             gps, gps, sats, _pwrState);

    bool sent1 = GsmNode::sendSMS(SMS_NUMBER_1, sms);
    bool sent2 = GsmNode::sendSMS(SMS_NUMBER_2, sms);
    Serial.printf("[GPS]  SMS to %s : %s\n", SMS_NUMBER_1, sent1 ? "OK" : "FAIL");
    Serial.printf("[GPS]  SMS to %s : %s\n", SMS_NUMBER_2, sent2 ? "OK" : "FAIL");
}

// ─────────────────────────────────────────────────────────────────────
// _handlePwrFrame — act on PWR,x from ATmega
// ─────────────────────────────────────────────────────────────────────
static void _handlePwrFrame(const char *state) {
    strlcpy(_pwrState, state, sizeof(_pwrState));
    Serial.print(F("[PWR]  State from ATmega: ")); Serial.println(state);

    char payload[48];
    snprintf(payload, sizeof(payload), "{\"pwr\":\"%s\"}", state);
    if (WifiMqtt::mqttConnected())
        WifiMqtt::publish("blackbox/power", payload);

    if (strcmp(state, "CRIT") == 0 && GsmNode::available()) {
        const TelData &t = UnoLink::telemetry();
        char sms[120];
        snprintf(sms, sizeof(sms),
                 "WARNING: SentinelBox battery critical (%.2fV). "
                 "Device entering long-sleep mode.", t.battV);
        GsmNode::sendSMS(SMS_NUMBER_1, sms);
    }
}

// ─────────────────────────────────────────────────────────────────────
// _sendTimeToAtmega — send GPS time string for DS3231 sync
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
