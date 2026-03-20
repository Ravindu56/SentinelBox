// leds.h — RGB LED + buzzer non-blocking state machine
#pragma once
#include <Arduino.h>
#include "hazards.h"
#include "../../include/config.h"

namespace Leds {
  void init();
  void update(uint8_t hazardFlags);   // call every loop; manages LED
  void beep(uint16_t ms);             // blocking beep (short, rare)
  void beepAsync(uint16_t onMs, uint8_t times); // non-blocking queued beep
  void tickAsync();                   // call every loop for async beep
}
