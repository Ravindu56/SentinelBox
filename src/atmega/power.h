// power.h — Battery monitor, watchdog, sleep, power state machine
#pragma once
#include <Arduino.h>
#include "../../include/config.h"

// Power operating modes
enum PowerMode : uint8_t {
    PWR_MODE_MAINS   = PWR_MAINS_NORMAL,
    PWR_MODE_BATT    = PWR_BATTERY_IDLE,
    PWR_MODE_EMERG   = PWR_EMERGENCY,
    PWR_MODE_CRIT    = PWR_CRITICAL_BATT
};

namespace Power {
    void initWatchdog();
    void feedWatchdog();

    // Core sleep — blocks for approx sleepMs, using stacked 8s WDT cycles
    // wakeOnInt: if true, also enables INT0 on D2 for immediate wake
    void sleepMs(uint32_t ms, bool wakeOnInt = false);

    // Power state machine — call once per main loop tick
    // Returns current PowerMode; updates internal state
    PowerMode updateMode(uint8_t hazardFlags);

    // Wake ESP32 via D2 HIGH pulse (50ms)
    void wakeESP32();

    // Battery
    float batteryVoltage();
    void batteryStatusStr(char *buf, uint8_t bufLen);
    bool isCritical();        // true if voltage < BATT_CRITICAL_V
    bool isOnMains();         // true if TP4056 CHRG pin = charging/mains
}
