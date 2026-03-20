// =====================================================================
// main.cpp — ESP32 WROOM Utility Node
// Disaster-Proof BlackBox  |  EC6020 Embedded Systems Design
//
// Modules:
//   gps_parser  — NEO-6M on UART2 (GPIO16/17)
//   uno_link    — ATmega link on UART1 (GPIO4/5)
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
#include "wifi_mqtt.h"
#include "web_server.h"

// ── millis scheduler ─────────────────────────────────────────────────
static unsigned long _lastDashPush = 0;

// ── MQTT incoming command callback ───────────────────────────────────
void onMqttMessage(char *topic, byte *payload, unsigned int len) {
  // Commands via MQTT blackbox/cmd topic
  // e.g. payload: "STATUS" / "LOCATION" / "RESET"
  char cmd[32]; uint8_t l = min((unsigned int)31, len);
  strncpy(cmd, (char*)payload, l); cmd[l] = '\0';

  char reply[100];
  const TelData &t = UnoLink::telemetry();
  char gps[32]; GpsParser::coordStr(gps, sizeof(gps));

  if (strstr(cmd, "STATUS")) {
    snprintf(reply, sizeof(reply),
             "T:%.1f H:%.0f W:%d G:%d GPS:%s B:%.1fV",
             t.tempC, t.humidity, t.water, t.mq2, gps, t.battV);
    WifiMqtt::publish(MQTT_TOPIC_EVT, reply);
  } else if (strstr(cmd, "LOCATION")) {
    snprintf(reply, sizeof(reply), "GPS:%s", gps);
    WifiMqtt::publish(MQTT_TOPIC_EVT, reply);
  }
}

// ─────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);  // USB debug only
  Serial.println(F("\n[ESP32] BlackBox utility node booting..."));

  // Initialise all modules
  GpsParser::init();
  UnoLink::init();
  WifiMqtt::init();
  WifiMqtt::setCallback(onMqttMessage);
  WebDash::init();

  // OTA (over-the-air firmware update for ESP32)
  ArduinoOTA.setHostname("blackbox-esp32");
  ArduinoOTA.onStart([]() { Serial.println(F("[OTA] Start")); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[OTA] Error %u\n", e);
  });
  ArduinoOTA.begin();

  Serial.print(F("[WiFi] IP: "));
  Serial.println(WifiMqtt::localIP());
  Serial.println(F("[ESP32] Ready"));
}

// ─────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────
void loop() {
  // ── Non-blocking tick for all modules ─────────────────────────────
  GpsParser::tick();    // decode GPS bytes
  UnoLink::tick();      // receive from ATmega, send GPS back
  WifiMqtt::tick();     // maintain WiFi/MQTT
  ArduinoOTA.handle();  // OTA check

  unsigned long now = millis();

  // ── Process new telemetry from ATmega ─────────────────────────────
  const TelData &tel = UnoLink::telemetry();
  if (tel.fresh) {
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));

    // Build JSON for MQTT
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

  // ── Process event from ATmega ──────────────────────────────────────
  const char *evt = UnoLink::lastEvent();
  if (evt[0] != '\0') {
    char payload[160];
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
    snprintf(payload, sizeof(payload),
             "{\"event\":\"%s\",\"gps\":\"%s\"}", evt, gps);
    WifiMqtt::publish(MQTT_TOPIC_EVT, payload);
    UnoLink::clearEvent();
  }

  // ── Push dashboard update every 2s ────────────────────────────────
  if (now - _lastDashPush >= 2000) {
    _lastDashPush = now;
    char gps[32]; GpsParser::coordStr(gps, sizeof(gps));
    WebDash::pushUpdate(tel, gps, GpsParser::sats());
  }
}
