#pragma once
#include <Arduino.h>
#include "hazards.h"
#include "../../include/config.h"

namespace Leds {
    void init();
    void update(uint8_t hazardFlags);
    void beep(uint16_t ms);                               // blocking
    void beepAsync(uint16_t onMs, uint8_t times);         // async default freq
    void beepAsync(uint16_t onMs, uint8_t times, uint16_t freq); // async custom freq
    void tickAsync();                                     // call every loop
}
