// power.h — Battery monitor, watchdog, sleep
#pragma once
#include <Arduino.h>
#include "../../include/config.h"

namespace Power {
  void    initWatchdog();
  void    feedWatchdog();
  void    enterSleep();          // Power-Down; wakes on INT0 or WDT

  float   batteryVoltage();      // Volts (via divider)
  // Fills buf: "CHARGING" | "FULL" | "ON_BATT" | "LOW_BATT"
  void    batteryStatusStr(char *buf, uint8_t bufLen);
}
