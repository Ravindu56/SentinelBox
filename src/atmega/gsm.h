// gsm.h — SIM800L GSM/GPRS module (AT commands, SMS)
#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "../../include/config.h"

namespace GSM {
  void init();                      // begin SoftwareSerial, detect baud
  bool available();                 // true after successful init

  // Send raw AT command; returns true if 'expect' appears in response
  bool sendCmd(const __FlashStringHelper *cmd, const char *expect,
               uint16_t timeoutMs = 2000);
  bool sendCmd(const char *cmd, const char *expect,
               uint16_t timeoutMs = 2000);

  // Send SMS to number; returns true on success
  bool sendSMS(const char *number, const char *msg);

  // Flush incoming SIM800L serial (call in loop for incoming SMS)
  void tick();

  // Returns last received line (for incoming SMS command parsing)
  const char* lastLine();
  void        clearLastLine();
}
