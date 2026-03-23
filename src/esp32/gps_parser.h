// gps_parser.h — NEO-6M GPS on UART2 (GPIO16/17)
#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "../../include/config.h"

namespace GpsParser {
    void    init();
    void    tick();              // feed GPS bytes; call every loop()
    bool    valid();
    float   lat();
    float   lon();
    uint8_t sats();

    // Fills buf "lat,lon" or "0.000000,0.000000"
    void    coordStr(char *buf, uint8_t bufLen);

    // Returns true if GPS has a valid date+time fix
    bool    hasTime();

    // Fills buf "YYYY-MM-DDTHH:MM:SS" (ISO 8601, needs ≥ 20 chars)
    // buf[0] = '\0' if no time fix available
    void    timeStr(char *buf, uint8_t bufLen);
}
