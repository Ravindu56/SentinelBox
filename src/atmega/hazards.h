// hazards.h — Hazard detection + flag management
#pragma once
#include <Arduino.h>
#include "sensors.h"

// Hazard bitmask definitions
#define HZ_QUAKE    (1 << 0)
#define HZ_FLOOD    (1 << 1)
#define HZ_HIGHTEMP (1 << 2)
#define HZ_GAS      (1 << 3)
#define HZ_FIRE     (1 << 4)
#define HZ_PANIC    (1 << 5)
#define HZ_HIGHHUM  (1 << 6)

// Critical hazards that trigger SMS immediately
#define HZ_CRITICAL (HZ_PANIC | HZ_FIRE | HZ_GAS | HZ_FLOOD)

namespace Hazards {
  uint8_t compute(const SensorData &d);
  // Writes human-readable text into buf (PROGMEM-safe)
  void    flagsToStr(uint8_t flags, char *buf, uint8_t bufLen);
  bool    isCritical(uint8_t flags);
}
