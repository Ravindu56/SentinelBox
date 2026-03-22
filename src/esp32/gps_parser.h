// gps_parser.h — NEO-6M GPS on UART2 (GPIO16/17)
#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "../../include/config.h"

namespace GpsParser {
  void init();
  void tick();               // feed GPS bytes; call every loop
  bool valid();
  float    lat();
  float    lon();
  uint8_t  sats();
  // Fills buf "lat,lon" or "0.000000,0.000000"
  void coordStr(char *buf, uint8_t bufLen);
}
