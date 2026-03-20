#include "sensors.h"

static DHT _dht(PIN_DHT, DHT22);

void Sensors::init() {
  pinMode(PIN_VIB,       INPUT);
  pinMode(PIN_BTN_PANIC, INPUT_PULLUP);
  _dht.begin();
}

void Sensors::read(SensorData &d) {
  d.tempC    = _dht.readTemperature();
  d.humidity = _dht.readHumidity();
  d.water    = analogRead(PIN_WATER);
  d.mq2      = analogRead(PIN_MQ2);
  d.flame    = analogRead(PIN_FLAME);
  d.vib      = digitalRead(PIN_VIB);
  d.panic    = (digitalRead(PIN_BTN_PANIC) == LOW);
}

bool Sensors::dhtOk(const SensorData &d) {
  return !isnan(d.tempC) && !isnan(d.humidity);
}
