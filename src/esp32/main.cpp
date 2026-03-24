// =====================================================================
// main.cpp — ESP32 WROOM Utility Node
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
//
// UART allocation:
//   UART0 (Serial)  — USB debug monitor        GPIO1 TX / GPIO3 RX
//   UART1 (Serial1) — SIM800L GSM              GPIO13 RX / GPIO14 TX
//   UART2 (Serial2) — NEO-6M GPS              GPIO16 RX / GPIO17 TX
//   UnoLink         — ATmega link              GPIO4  RX / GPIO5  TX
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

// ── Scheduler ───────────────────────────────────────────────────
static unsigned long _lastDashPush  = 0;
static unsigned long _lastHeartbeat = 0;
static unsigned long _lastSmsSent   = 0;
static unsigned long _lastGpsSent   = 0;
static unsigned long _lastGpsReport = 0;
static bool          _gsmInitDone   = false;

// ── Power state ──────────────────────────────────────────────────
static char _pwrState[8] = "MAINS";

// ── Forward declarations ─────────────────────────────────────────────
static void _handlePwrFrame(const char *state);
static void _sendTimeToAtmega();
static void _reportGpsLocation();

// ── MQTT incoming command callback ─────────────────────────────────
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
                 "SentinelBox Status\n"
                 "T:%.1fC H:%.0f%% W:%d G:%d F:%d V:%d\n"
                 "Batt:%.1fV(%s) PWR:%s\n"
                 "GPS:%s",
                 t.tempC, t.humidity, t.water, t.mq2, t.flame, t.vib,
                 t.battV, t.battSt, _pwrState, gps);
        WifiMqtt::publish(MQTT_TOPIC_EVT, reply);
        if (GsmNode::available())
            GsmNode::sendSMS(SMS_NUMBER_1, reply);

    } else if (strstr(cmd, "LOCATION")) {
        snprintf(reply, sizeof(reply),
                 "SentinelBox Location\n"
                 "GPS:%s\n"
                 "Map:maps.google.com/?q=%s\n"
                 "Sats:%u PWR:%s",
                 gps, gps, GpsParser::sats(), _pwrState);
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
    UnoLink::init();      // UART1: GPIO4  RX / GPIO5  TX — ATmega @ 115200
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
    // ── Deferred GSM init — 3s after boot so Serial is stable ────
    if (!_gsmInitDone && millis() >= 3000UL) {
        _gsmInitDone = true;
        GsmNode::init();   // UART1: GPIO13 RX / GPIO14 TX — SIM800L @ 9600
        Serial.printf("[GSM]  %s\n",
            GsmNode::available() ? "SIM800L ready" : "Not found — SMS disabled");
    }

    // ── Non-blocking module ticks ───────────────────────────
    GpsParser::tick();
    UnoLink::tick();
    GsmNode::tick();
    WifiMqtt::tick();
    ArduinoOTA.handle();

    unsigned long now = millis();
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
    const TelData &tel = UnoLink::telemetry();

    // ── Power coordination frames from ATmega ──────────────────
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

    // ── Send GPS time to ATmega every 60s when locked ──────────
    if (GpsParser::hasTime() && (now - _lastGpsSent >= 60000UL)) {
        _lastGpsSent = now;
        _sendTimeToAtmega();
    }

    // ── 1-minute GPS + sensor report → Serial + SMS both numbers ──
    if (now - _lastGpsReport >= 60000UL) {
        _lastGpsReport = now;
        _reportGpsLocation();
    }

    // ── New telemetry frame from ATmega ─────────────────────
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

    // ── Hazard event from ATmega ─────────────────────────
    const char *evt = UnoLink::lastEvent();
    if (evt[0] != '\0') {
        // ── MQTT event payload (JSON)
        char payload[200];
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"%s\",\"t\":%.1f,\"h\":%.0f,"
                 "\"w\":%d,\"g\":%d,\"f\":%d,\"v\":%d,"
                 "\"bv\":%.2f,\"bs\":\"%s\","
                 "\"gps\":\"%s\",\"pwr\":\"%s\"}",
                 evt, tel.tempC, tel.humidity,
                 tel.water, tel.mq2, tel.flame, tel.vib,
                 tel.battV, tel.battSt, gps, _pwrState);
        WifiMqtt::publish(MQTT_TOPIC_EVT, payload);

        bool isCritical = (strstr(evt, "NORMAL") == nullptr);
        bool isPanic    = (strstr(evt, "PANIC")  != nullptr);
        bool cooldownOk = (now - _lastSmsSent) >= SMS_COOLDOWN_MS;

        if (GsmNode::available() && isCritical && (cooldownOk || isPanic)) {
            // ── Full sensor + location disaster SMS ──────────────
            char sms[160];
            snprintf(sms, sizeof(sms),
                     "%sALERT: %s\n"
                     "T:%.1fC H:%.0f%% W:%d\n"
                     "Gas:%d Flame:%d Vib:%d\n"
                     "Batt:%.1fV(%s) %s\n"
                     "GPS:%s\n"
                     "Map:maps.google.com/?q=%s",
                     isPanic ? "[PANIC] " : "",
                     evt,
                     tel.tempC, tel.humidity, tel.water,
                     tel.mq2, tel.flame, tel.vib,
                     tel.battV, tel.battSt, _pwrState,
                     gps, gps);

            // Send to BOTH numbers independently — don't gate #2 on #1 result
            bool s1 = GsmNode::sendSMS(SMS_NUMBER_1, sms);
            bool s2 = GsmNode::sendSMS(SMS_NUMBER_2, sms);

            Serial.printf("[GSM]  %s SMS — #1:%s #2:%s\n",
                isPanic ? "PANIC" : "Alert",
                s1 ? "OK" : "FAIL", s2 ? "OK" : "FAIL");

            if (!isPanic) _lastSmsSent = now;
        }
        UnoLink::clearEvent();
    }

    // ── Incoming SMS command handler ──────────────────────
    const char *gsmLine = GsmNode::lastLine();
    if (gsmLine[0] != '\0') {
        if (strstr(gsmLine, "STATUS")) {
            char reply[160];
            snprintf(reply, sizeof(reply),
                     "SentinelBox Status\n"
                     "Time:%s\n"
                     "T:%.1fC H:%.0f%% W:%d\n"
                     "Gas:%d Flame:%d Vib:%d\n"
                     "Batt:%.1fV(%s) %s\n"
                     "GPS:%s",
                     tel.ts,
                     tel.tempC, tel.humidity, tel.water,
                     tel.mq2, tel.flame, tel.vib,
                     tel.battV, tel.battSt, _pwrState,
                     gps);
            GsmNode::sendSMS(SMS_NUMBER_1, reply);

        } else if (strstr(gsmLine, "LOCATION")) {
            char reply[120];
            snprintf(reply, sizeof(reply),
                     "SentinelBox Location\n"
                     "GPS:%s\n"
                     "Map:maps.google.com/?q=%s\n"
                     "Sats:%u PWR:%s",
                     gps, gps, GpsParser::sats(), _pwrState);
            GsmNode::sendSMS(SMS_NUMBER_1, reply);

        } else if (strstr(gsmLine, "BATT")) {
            char reply[80];
            snprintf(reply, sizeof(reply),
                     "Battery: %.2fV (%s)\nMode: %s",
                     tel.battV, tel.battSt, _pwrState);
            GsmNode::sendSMS(SMS_NUMBER_1, reply);
        }
        GsmNode::clearLastLine();
    }

    // ── Web dashboard SSE push every 2s ────────────────────
    if (now - _lastDashPush >= 2000UL) {
        _lastDashPush = now;
        WebDash::pushUpdate(tel, gps, GpsParser::sats());
    }

    // ── Hourly heartbeat SMS — full sensor snapshot ────────────
    if (now - _lastHeartbeat >= HEARTBEAT_MS) {
        _lastHeartbeat = now;
        if (GsmNode::available()) {
            char hb[160];
            snprintf(hb, sizeof(hb),
                     "SentinelBox HB\n"
                     "Time:%s\n"
                     "T:%.1fC H:%.0f%% W:%d\n"
                     "Gas:%d Flame:%d\n"
                     "Batt:%.1fV(%s) %s\n"
                     "WiFi:%s MQTT:%s Sats:%u\n"
                     "GPS:%s",
                     tel.ts,
                     tel.tempC, tel.humidity, tel.water,
                     tel.mq2, tel.flame,
                     tel.battV, tel.battSt, _pwrState,
                     WifiMqtt::wifiConnected() ? "OK" : "FAIL",
                     WifiMqtt::mqttConnected() ? "OK" : "FAIL",
                     GpsParser::sats(), gps);
            GsmNode::sendSMS(SMS_NUMBER_1, hb);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// _reportGpsLocation — every 60s: Serial print + SMS with sensor data
// ─────────────────────────────────────────────────────────────────────
static void _reportGpsLocation() {
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
    uint8_t sats = GpsParser::sats();
    bool    lock = GpsParser::valid();
    const TelData &tel = UnoLink::telemetry();

    // ── Serial monitor print ─────────────────────────────────
    Serial.println(F("─────────────────────────────────────"));
    Serial.print(F("[RPT]  @ ")); Serial.print(millis()/1000); Serial.println(F("s"));
    Serial.printf("[RPT]  T:%.1fC H:%.0f%% W:%d Gas:%d Flame:%d Vib:%d\n",
                  tel.tempC, tel.humidity, tel.water, tel.mq2, tel.flame, tel.vib);
    Serial.printf("[RPT]  Batt:%.1fV(%s) PWR:%s\n",
                  tel.battV, tel.battSt, _pwrState);
    if (lock) {
        Serial.printf("[GPS]  %s  Sats:%u\n", gps, sats);
        Serial.printf("[GPS]  Map: https://maps.google.com/?q=%s\n", gps);
    } else {
        Serial.printf("[GPS]  No fix  Sats visible:%u\n", sats);
    }
    Serial.println(F("─────────────────────────────────────"));

    if (!GsmNode::available()) {
        Serial.println(F("[GSM]  SMS skipped — GSM not available"));
        return;
    }
    if (!lock) {
        Serial.println(F("[GPS]  SMS skipped — no GPS fix yet"));
        return;
    }

    // ── SMS with full sensor snapshot + location ───────────────
    char sms[160];
    snprintf(sms, sizeof(sms),
             "SentinelBox Report\n"
             "T:%.1fC H:%.0f%% W:%d\n"
             "Gas:%d Flame:%d Vib:%d\n"
             "Batt:%.1fV(%s) %s\n"
             "GPS:%s Sats:%u\n"
             "Map:maps.google.com/?q=%s",
             tel.tempC, tel.humidity, tel.water,
             tel.mq2, tel.flame, tel.vib,
             tel.battV, tel.battSt, _pwrState,
             gps, sats, gps);

    bool sent1 = GsmNode::sendSMS(SMS_NUMBER_1, sms);
    bool sent2 = GsmNode::sendSMS(SMS_NUMBER_2, sms);
    Serial.printf("[GPS]  SMS — #1:%s  #2:%s\n",
                  sent1 ? "OK" : "FAIL", sent2 ? "OK" : "FAIL");
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
        char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
        char sms[120];
        snprintf(sms, sizeof(sms),
                 "WARNING: SentinelBox battery critical\n"
                 "Voltage: %.2fV\n"
                 "GPS:%s\n"
                 "Device entering long-sleep mode.",
                 t.battV, gps);
        GsmNode::sendSMS(SMS_NUMBER_1, sms);
        GsmNode::sendSMS(SMS_NUMBER_2, sms);
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
