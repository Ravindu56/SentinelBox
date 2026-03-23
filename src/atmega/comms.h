// comms.h — ATmega ↔ ESP32 UART protocol
// =====================================================================
// Frames OUT (ATmega → ESP32):
//   TEL,ts,tempC,hum,water,mq2,flame,vib,panic,flags,battV,battSt
//   EVT,ts,flagText,gps
//   PWR,MAINS|BATT|CRIT
//   SLEEP
//   WAKE
//   PONG
//
// Frames IN (ESP32 → ATmega):
//   GPS,lat,lon,sats
//   PING
//   ACK,<millis>
//   TIME,YYYY-MM-DDTHH:MM:SS
// =====================================================================
#pragma once
#include <Arduino.h>
#include "sensors.h"
#include <avr/wdt.h>

namespace Comms {
    void     init();     // call once in setup()
    void     tick();     // call every loop() — parses incoming ESP32 data

    // ── Outbound frames ───────────────────────────────────────────────
    void     sendTel(const char *ts, const SensorData &d,
                     uint8_t flags, float battV, const char *battSt);
    void     sendEvt(const char *ts, const char *flagText, const char *gps);
    void     sendSleep();               // ESP32 → deep sleep
    void     sendWake();                // ESP32 → wake up (after GPIO pulse)
    void     sendPwr(const char *st);   // "MAINS" | "BATT" | "CRIT"

    // ── ACK health check ──────────────────────────────────────────────
    uint32_t lastAckAge();              // ms since last ACK from ESP32
    bool     esp32Responsive();         // true if ACK received within 30s

    // ── GPS state ─────────────────────────────────────────────────────
    const char* lastGPS();
    bool        gpsValid();

    // ── Internal ──────────────────────────────────────────────────────
    void     _processLine();            // called by tick()
}
