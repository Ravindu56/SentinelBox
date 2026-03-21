// gsm.cpp — GSM migrated to ESP32 utility node
// ATmega stub: all calls are no-ops
#include "gsm.h"

void        GSM::init()          {}   // GSM is on ESP32 — do nothing
bool        GSM::available()     { return false; }
bool        GSM::sendCmd(const __FlashStringHelper*, const char*, uint16_t) { return false; }
bool        GSM::sendCmd(const char*, const char*, uint16_t)                { return false; }
bool        GSM::sendSMS(const char*, const char*)                          { return false; }
void        GSM::tick()          {}
const char* GSM::lastLine()      { return ""; }
void        GSM::clearLastLine() {}
