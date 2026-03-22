// uno_link.h
#pragma once
#include <Arduino.h>
#include "../../include/config.h"
#include "gps_parser.h"

struct TelData {
  char    ts[20];       // "2026-03-22 00:53:06"
  float   tempC;
  float   humidity;
  int     water;
  int     mq2;
  int     flame;
  uint8_t vib;
  uint8_t panic;
  uint8_t flags;
  float   battV;
  char    battSt[12];   // "FULL" / "ON_BATT" / "CHARGING" / "LOW_BATT"
  bool    fresh;        // true for exactly one loop() after new TEL received
};

namespace UnoLink {
  void            init();
  void            tick();
  void            sendGPS();
  const TelData&  telemetry();
  const char*     lastEvent();
  void            clearEvent();
}
