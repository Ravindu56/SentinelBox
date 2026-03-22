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
// millis()-based scheduling — NO blocking delay() in main loop.
// =====================================================================

#include <Arduino.h>
#include <ArduinoOTA.h>
#include "../../include/config.h"

#include "gps_parser.h"
#include "uno_link.h"
#include "gsm_node.h"
#include "wifi_mqtt.h"
#include "web_server.h"

// ── Scheduler ────────────────────────────────────────────────────────
static unsigned long _lastDashPush  = 0;
static unsigned long _lastHeartbeat = 0;
static unsigned long _lastSmsSent   = 0;
static bool _gsmInitDone = false;

// ── MQTT incoming command callback ───────────────────────────────────
void onMqttMessage(char *topic, byte *payload, unsigned int len) {
  char cmd[32];
  uint8_t l = min((unsigned int)31, len);
  strncpy(cmd, (char*)payload, l); cmd[l] = '\0';

  const TelData &t = UnoLink::telemetry();
  char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
  char reply[160];

  if (strstr(cmd, "STATUS")) {
    snprintf(reply, sizeof(reply),
             "T:%.1f H:%.0f W:%d G:%d GPS:%s B:%.1fV(%s)",
             t.tempC, t.humidity, t.water, t.mq2,
             gps, t.battV, t.battSt);
    WifiMqtt::publish(MQTT_TOPIC_EVT, reply);

    // SMS reply if GSM available
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
//  SETUP
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n[ESP32] BlackBox utility node booting..."));

  GpsParser::init();    // UART2: GPIO16/17 — NEO-6M GPS @ 9600
  UnoLink::init();      // UART1: GPIO4/5   — ATmega link @ 115200
  // GsmNode::init();      // UART0: GPIO26/27 — SIM800L GSM @ 9600
  WifiMqtt::init();
  WifiMqtt::setCallback(onMqttMessage);
  WebDash::init();

  // OTA firmware updates over WiFi
  ArduinoOTA.setHostname("blackbox-esp32");
  ArduinoOTA.onStart([]()  { Serial.println(F("[OTA] Start")); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[OTA] Error %u\n", e);
  });
  ArduinoOTA.begin();

  Serial.print(F("[WiFi] IP: "));
  Serial.println(WifiMqtt::localIP());
  Serial.printf("[GSM]  %s\n", GsmNode::available() ? "Ready" : "FAIL");
  Serial.println(F("[ESP32] Ready\n"));
}

// ─────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────
void loop() {
    if (!_gsmInitDone && millis() >= 3000UL) {
      _gsmInitDone = true;
      GsmNode::init();
      Serial.printf("[GSM]  %s\n",
          GsmNode::available() ? "Ready" : "Not connected — SMS disabled");
  }

  // ── Non-blocking ticks ────────────────────────────────────────────
  GpsParser::tick();
  UnoLink::tick();
  GsmNode::tick();
  WifiMqtt::tick();
  ArduinoOTA.handle();

  unsigned long now = millis();
  char gps[32]; GpsParser::coordStr(gps, sizeof(gps));

  // ── New telemetry frame from ATmega ───────────────────────────────
  const TelData &tel = UnoLink::telemetry();
  if (tel.fresh) {
    // Publish JSON to MQTT
    char json[256];
    snprintf(json, sizeof(json),
      "{\"ts\":\"%s\",\"t\":%.1f,\"h\":%.0f,"
      "\"w\":%d,\"g\":%d,\"f\":%d,\"v\":%d,"
      "\"flags\":%u,\"bv\":%.2f,\"bs\":\"%s\","
      "\"gps\":\"%s\",\"sats\":%u}",
      tel.ts, tel.tempC, tel.humidity,
      tel.water, tel.mq2, tel.flame, tel.vib,
      tel.flags, tel.battV, tel.battSt,
      gps, GpsParser::sats());
    WifiMqtt::publish(MQTT_TOPIC_TEL, json);
  }

  // ── Event from ATmega (hazard detected) ───────────────────────────
  const char *evt = UnoLink::lastEvent();
  if (evt[0] != '\0') {
    // Publish to MQTT
    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"%s\",\"gps\":\"%s\"}", evt, gps);
    WifiMqtt::publish(MQTT_TOPIC_EVT, payload);

    // SMS alert — only on non-NORMAL events, 5-min cooldown
    bool isCritical = (strstr(evt, "NORMAL") == nullptr);
    bool cooldownOk = (now - _lastSmsSent) >= SMS_COOLDOWN_MS;

    if (GsmNode::available() && isCritical && cooldownOk) {
      char sms[160];
      snprintf(sms, sizeof(sms),
               "ALERT:%s T:%.1fC H:%.0f%% GPS:%s B:%.1fV "
               "MAP:maps.google.com/?q=%s",
               evt, tel.tempC, tel.humidity,
               gps, tel.battV, gps);
      if (GsmNode::sendSMS(SMS_NUMBER_1, sms)) {
        GsmNode::sendSMS(SMS_NUMBER_2, sms);
        _lastSmsSent = now;
        Serial.println(F("[GSM] Alert SMS sent to both numbers"));
      }
    }

    UnoLink::clearEvent();
  }

  // ── GSM: incoming SMS command handler ─────────────────────────────
  // (e.g. someone texts "STATUS" or "LOCATION" to the SIM card)
  const char *gsmLine = GsmNode::lastLine();
  if (gsmLine[0] != '\0') {
    if (strstr(gsmLine, "STATUS")) {
      char reply[160];
      snprintf(reply, sizeof(reply),
               "STATUS T:%.1f H:%.0f W:%d G:%d F:%d GPS:%s B:%.1fV(%s)",
               tel.tempC, tel.humidity, tel.water,
               tel.mq2, tel.flame, gps, tel.battV, tel.battSt);
      GsmNode::sendSMS(SMS_NUMBER_1, reply);
    } else if (strstr(gsmLine, "LOCATION")) {
      char reply[100];
      snprintf(reply, sizeof(reply),
               "LOC:%s maps.google.com/?q=%s", gps, gps);
      GsmNode::sendSMS(SMS_NUMBER_1, reply);
    }
    GsmNode::clearLastLine();
  }

  // ── Web dashboard SSE push every 2s ───────────────────────────────
  if (now - _lastDashPush >= 2000) {
    _lastDashPush = now;
    WebDash::pushUpdate(tel, gps, GpsParser::sats());
  }

  // ── Hourly heartbeat SMS ───────────────────────────────────────────
  if (now - _lastHeartbeat >= HEARTBEAT_MS) {
    _lastHeartbeat = now;
    if (GsmNode::available()) {
      char hb[160];
      snprintf(hb, sizeof(hb),
               "HB:%s GPS:%s B:%.1fV(%s) WiFi:%s MQTT:%s SD:OK",
               tel.ts, gps, tel.battV, tel.battSt,
               WifiMqtt::wifiConnected() ? "OK" : "FAIL",
               WifiMqtt::mqttConnected() ? "OK" : "FAIL");
      GsmNode::sendSMS(SMS_NUMBER_1, hb);
    }
  }
}
