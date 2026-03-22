#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

RTC_DS3231 rtc;

// ── helpers ──────────────────────────────────────────────────────────
void pass(const __FlashStringHelper* msg) {
    Serial.print(F("  [PASS] "));
    Serial.println(msg);
}

void fail(const __FlashStringHelper* msg) {
    Serial.print(F("  [FAIL] "));
    Serial.println(msg);
}

// ── tests ─────────────────────────────────────────────────────────────

void test_i2c_scan() {
    Serial.println(F("\n--- I2C Scan ---"));
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F("  Device at 0x"));
            if (addr < 16) Serial.print(F("0"));
            Serial.println(addr, HEX);
            found++;
        }
    }
    if (found == 0) fail(F("No I2C devices found — check A4/A5 wiring"));
    else            pass(F("I2C device(s) found"));
}

void test_rtc_begin() {
    Serial.println(F("\n--- RTC Init ---"));
    if (!rtc.begin()) {
        fail(F("rtc.begin() failed — DS3231 not responding"));
        return;
    }
    pass(F("rtc.begin() OK"));
}

void test_rtc_lost_power() {
    Serial.println(F("\n--- RTC Power Status ---"));
    if (rtc.lostPower()) {
        Serial.println(F("  [WARN] RTC lost power — time is invalid"));
        Serial.println(F("         Setting to compile time..."));
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        pass(F("Time set to compile time"));
    } else {
        pass(F("RTC has valid time (battery OK)"));
    }
}

void test_rtc_read_time() {
    Serial.println(F("\n--- RTC Read Time ---"));
    DateTime now = rtc.now();

    char buf[24];
    snprintf(buf, sizeof(buf),
             "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());

    Serial.print(F("  Current time: "));
    Serial.println(buf);

    // basic sanity: year should be >= 2024
    if (now.year() >= 2024) pass(F("Year looks valid"));
    else                    fail(F("Year invalid — RTC may need battery"));
}

void test_rtc_tick() {
    Serial.println(F("\n--- RTC Tick Test (3 seconds) ---"));
    DateTime t1 = rtc.now();
    delay(3000);
    DateTime t2 = rtc.now();

    int32_t diff = (int32_t)t2.unixtime() - (int32_t)t1.unixtime();
    Serial.print(F("  Elapsed seconds: "));
    Serial.println(diff);

    if (diff >= 2 && diff <= 4) pass(F("RTC is ticking correctly"));
    else                        fail(F("RTC not ticking — check CR2032 coin cell"));
}

void test_rtc_temperature() {
    Serial.println(F("\n--- DS3231 Internal Temp ---"));
    float t = rtc.getTemperature();
    Serial.print(F("  DS3231 temp: "));
    Serial.print(t);
    Serial.println(F(" C"));

    if (t > 0.0f && t < 85.0f) pass(F("Temperature reading looks valid"));
    else                        fail(F("Temperature out of range"));
}

// ── main ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Wire.begin();
    delay(1000);

    Serial.println(F("========================================="));
    Serial.println(F("  RTC Test — DS3231"));
    Serial.println(F("========================================="));

    test_i2c_scan();
    test_rtc_begin();
    test_rtc_lost_power();
    test_rtc_read_time();
    test_rtc_tick();
    test_rtc_temperature();

    Serial.println(F("\n========================================="));
    Serial.println(F("  Done. Check results above."));
    Serial.println(F("========================================="));
}

void loop() {}
