// gsm_node.h — SIM800L GSM on ESP32 UART0 (GPIO26 RX / GPIO27 TX)
// Handles: init, SMS send, tick (URC reader), SMS command response
#pragma once
#include <Arduino.h>
#include "../../include/config.h"

namespace GsmNode {
  void        init();
  bool        available();
  bool        sendSMS(const char *number, const char *msg);
  void        tick();           // non-blocking, call every loop()
  const char* lastLine();       // last URC / response line received
  void        clearLastLine();
}
