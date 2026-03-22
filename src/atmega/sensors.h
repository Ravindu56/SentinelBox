// sensors.h — Sensor reading module
#pragma once
#include <Arduino.h>
#include <DHT.h>
#include "../../include/config.h"

struct SensorData {
  float   tempC;      // DHT22 temperature (NaN if error)
  float   humidity;   // DHT22 humidity    (NaN if error)
  int     water;      // ADC 0-1023
  int     mq2;        // ADC 0-1023
  int     flame;      // ADC 0-1023 (LOW = fire)
  uint8_t vib;        // HIGH = vibration
  bool    panic;      // true = panic button pressed
};

namespace Sensors {
  void    init();
  void    read(SensorData &d);
  bool    dhtOk(const SensorData &d);
}
