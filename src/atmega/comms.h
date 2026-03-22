// comms.h — ATmega ↔ ESP32 serial protocol
// Format: "TYPE,field1,field2,...\n"
// TEL → ATmega sends telemetry to ESP32
// EVT → ATmega sends event to ESP32
// GPS → ESP32 sends GPS fix to ATmega
// PING/PONG → health check
#pragma once
#include <Arduino.h>
#include "sensors.h"
#include <avr/wdt.h>

namespace Comms {
  void init();    // call in setup()
  void tick();    // call every loop — reads incoming data from ESP32

  // Send telemetry row to ESP32
  void sendTel(const char *ts, const SensorData &d,
               uint8_t flags, float battV, const char *battSt);
  // Send hazard event to ESP32
  void sendEvt(const char *ts, const char *flagText, const char *gps);
  // Last GPS string received from ESP32 ("lat,lon" or "0,0")
  const char* lastGPS();
  bool        gpsValid();
}
