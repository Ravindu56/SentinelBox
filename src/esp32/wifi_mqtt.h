// wifi_mqtt.h — WiFi connection + MQTT publish
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "../../include/config.h"

namespace WifiMqtt {
  void init();             // connect WiFi; fall back to AP mode
  void tick();             // maintain WiFi + MQTT; call every loop
  bool mqttConnected();
  bool wifiConnected();
  void publish(const char *topic, const char *payload);
  // Register callback for incoming MQTT messages
  void setCallback(MQTT_CALLBACK_SIGNATURE);
  IPAddress localIP();
}
