// uno_link.h — UART1 link between ESP32 and ATmega
// GPIO4=RX, GPIO5=TX  (add voltage divider on ATmega TX → ESP32 RX)
#pragma once
#include <Arduino.h>
#include "gps_parser.h"
#include "../../include/config.h"

// Parsed telemetry from ATmega
struct TelData {
  char     ts[20];
  float    tempC;
  float    humidity;
  int      water, mq2, flame;
  uint8_t  vib;
  bool     panic;
  uint8_t  flags;
  float    battV;
  char     battSt[10];
  bool     fresh;    // true when new data received this tick
};

namespace UnoLink {
  void init();
  void tick();                     // receive from ATmega; call every loop

  // Send GPS to ATmega (call periodically)
  void sendGPS();

  const TelData& telemetry();      // last parsed telemetry
  // Last event string (empty if none since last call)
  const char*    lastEvent();
  void           clearEvent();
}
