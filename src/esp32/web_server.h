// web_server.h — Async web dashboard with Server-Sent Events (SSE)
#pragma once
#include <Arduino.h>
#include "uno_link.h"
#include "gps_parser.h"

namespace WebDash {
  void init();
  // Push latest telemetry to all SSE clients
  void pushUpdate(const TelData &tel, const char *gpsCoords, uint8_t sats);
}
