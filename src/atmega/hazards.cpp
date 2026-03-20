#include "hazards.h"
#include "../../include/config.h"

uint8_t Hazards::compute(const SensorData &d) {
  uint8_t f = 0;
  if (d.vib == HIGH)                              f |= HZ_QUAKE;
  if (d.water  > WATER_TH)                        f |= HZ_FLOOD;
  if (!isnan(d.tempC)    && d.tempC    > TEMP_FIRE_C) f |= HZ_HIGHTEMP;
  if (d.mq2    > MQ2_GAS_TH)                      f |= HZ_GAS;
  if (d.flame  < FLAME_TH)                        f |= HZ_FIRE;   // LOW = fire
  if (d.panic)                                    f |= HZ_PANIC;
  if (!isnan(d.humidity) && d.humidity > HUMID_MAX)   f |= HZ_HIGHHUM;
  return f;
}

void Hazards::flagsToStr(uint8_t f, char *buf, uint8_t bufLen) {
  buf[0] = '\0';
  if (f == 0) { strlcpy(buf, "NORMAL", bufLen); return; }
  if (f & HZ_QUAKE)   strlcat(buf, "QUAKE;",    bufLen);
  if (f & HZ_FLOOD)   strlcat(buf, "FLOOD;",    bufLen);
  if (f & HZ_HIGHTEMP)strlcat(buf, "HIGH_TEMP;",bufLen);
  if (f & HZ_GAS)     strlcat(buf, "GAS;",      bufLen);
  if (f & HZ_FIRE)    strlcat(buf, "FIRE;",     bufLen);
  if (f & HZ_PANIC)   strlcat(buf, "PANIC;",    bufLen);
  if (f & HZ_HIGHHUM) strlcat(buf, "HIGH_HUM;", bufLen);
}

bool Hazards::isCritical(uint8_t f) {
  return (f & HZ_CRITICAL) != 0;
}
