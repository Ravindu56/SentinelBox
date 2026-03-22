#include "wifi_mqtt.h"

static WiFiClient   _wifiClient;
static PubSubClient _mqtt(_wifiClient);
static bool         _apMode       = false;
static unsigned long _lastReconnect = 0;

// Exponential backoff (caps at 60 s)
static uint32_t _mqttBackoff = 2000;

void WifiMqtt::init() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    _apMode = true;
  }
  _mqtt.setServer(MQTT_HOST, MQTT_PORT);
  _mqtt.setBufferSize(512);   // larger buffer for JSON payloads
}

void WifiMqtt::tick() {
  if (!_apMode && WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect(); delay(100);
  }
  if (!_mqtt.connected()) {
    unsigned long now = millis();
    if (now - _lastReconnect >= _mqttBackoff) {
      _lastReconnect = now;
      char cid[24];
      snprintf(cid, sizeof(cid), "bb-esp32-%08X", (uint32_t)ESP.getEfuseMac());
      if (_mqtt.connect(cid)) {
        _mqtt.subscribe(MQTT_TOPIC_CMD);
        _mqttBackoff = 2000;  // reset on success
      } else {
        _mqttBackoff = min((uint32_t)60000, _mqttBackoff * 2);
      }
    }
  }
  _mqtt.loop();
}

bool WifiMqtt::mqttConnected() { return _mqtt.connected(); }
bool WifiMqtt::wifiConnected() { return WiFi.status() == WL_CONNECTED || _apMode; }

void WifiMqtt::publish(const char *topic, const char *payload) {
  if (_mqtt.connected()) _mqtt.publish(topic, payload);
}

void WifiMqtt::setCallback(MQTT_CALLBACK_SIGNATURE) {
  _mqtt.setCallback(callback);
}

IPAddress WifiMqtt::localIP() {
  return _apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
