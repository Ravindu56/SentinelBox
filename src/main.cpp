/*
 * =====================================================================
 * Disaster-Proof Black Box — SENSOR TEST MODE
 * No EEPROM | No GSM (SIM800L) | No ESP32
 * 
 * Active modules:
 *   ✅ DHT22        — temperature + humidity
 *   ✅ SW-420       — vibration
 *   ✅ Water sensor — flood detection
 *   ✅ MQ-2         — gas/smoke
 *   ✅ Flame sensor — fire detection
 *   ✅ Panic button — manual trigger
 *   ✅ RGB LED      — status indicator
 *   ✅ Buzzer       — local alert
 *   ✅ DS3231 RTC   — timestamps
 *   ✅ Micro SD     — CSV logging
 *   ✅ Battery ADC  — voltage + TP4056 status
 *   ✅ Watchdog     — system reliability
 *   ✅ Serial Monitor — live sensor output for calibration
 * =====================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <DHT.h>
#include <RTClib.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>

// ===================== PIN DEFINITIONS =====================
#define PIN_DHT         4
#define PIN_VIB         5
#define PIN_BUZZER      6
#define PIN_LED_R       7
#define PIN_LED_G       8
#define PIN_LED_B       9
#define SD_CS_PIN       10
#define PIN_BTN_PANIC   A3
#define PIN_WATER       A0
#define PIN_MQ2         A1
#define PIN_FLAME       A2
#define PIN_BATTERY     A6
#define PIN_TP_CHRG     A7

// ===================== THRESHOLDS (tune these!) =====================
// Watch Serial Monitor readings and adjust until detection is accurate
float TEMP_FIRE_C = 55.0;   // °C — raise temp above this to trigger
float HUMID_MAX   = 95.0;   // % humidity
int   MQ2_GAS_TH  = 450;    // 0–1023 — blow near MQ-2 to see value rise
int   WATER_TH    = 400;    // 0–1023 — dip sensor in water to calibrate
int   FLAME_TH    = 400;    // 0–1023 — flame value drops when fire detected

// ===================== TIMING =====================
const unsigned long LOG_INTERVAL_MS = 2000;   // print + log every 2s (fast for testing)

// ===================== GLOBALS =====================
#define DHTTYPE DHT22
DHT      dht(PIN_DHT, DHTTYPE);
RTC_DS3231 rtc;

unsigned long lastLogMs  = 0;
uint8_t       hazardFlags = 0;
File          logFile;

bool sdAvailable  = false;
bool rtcAvailable = false;

// ===================== WATCHDOG =====================
ISR(WDT_vect) { /* frozen → hardware reset */ }

void watchdog_enable() {
  cli();
  wdt_reset();
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  WDTCSR  = (1 << WDE) | (1 << WDP3) | (1 << WDP0); // 8s
  sei();
}

// ===================== LED + BUZZER =====================
void setLED(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r);
  digitalWrite(PIN_LED_G, g);
  digitalWrite(PIN_LED_B, b);
}

void buzzerBeep(uint16_t ms) {
  digitalWrite(PIN_BUZZER, LOW);
  delay(ms);
  digitalWrite(PIN_BUZZER, HIGH);
}

// Blink LED to signal a status without blocking
void blinkLED(bool r, bool g, bool b, uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    setLED(r, g, b);
    delay(150);
    setLED(false, false, false);
    delay(150);
  }
}

// ===================== RTC =====================
String isoTimeNow() {
  if (!rtcAvailable) return "0000-00-00 00:00:00";
  DateTime now = rtc.now();
  char buf[22];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  return String(buf);
}

// ===================== BATTERY =====================
float readBatteryVoltage() {
  int raw = analogRead(PIN_BATTERY);
  float vout = raw * (5.0 / 1023.0);
  return vout * 4.3;  // 33k/10k divider
}

String batteryStatus() {
  int chrg = analogRead(PIN_TP_CHRG);
  if (chrg < 200) return "CHARGING";
  float v = readBatteryVoltage();
  if (v > 4.1) return "FULL";
  if (v < 3.3) return "LOW_BATT";
  return "ON_BATT";
}

// ===================== SD CARD =====================
void sdInit() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("[SD]  FAIL — check CS=D10 + SPI wiring"));
    setLED(true, false, false);
    delay(500);
    sdAvailable = false;
    return;
  }
  logFile = SD.open("log.csv", FILE_WRITE);
  if (logFile && logFile.size() == 0) {
    logFile.println("time,tempC,hum,water,mq2,flame,vib,panic,flags,battV,battSt");
    logFile.flush();
  }
  sdAvailable = true;
  Serial.println(F("[SD]  OK — logging to log.csv"));
}

// ===================== HAZARD DETECTION =====================
uint8_t computeHazards(float tC, float h, int water, int mq2,
                        int flame, int vib, bool panic) {
  uint8_t f = 0;
  if (vib == HIGH)                       f |= (1 << 0);   // vibration/quake
  if (water > WATER_TH)                  f |= (1 << 1);   // flood
  if (!isnan(tC) && tC > TEMP_FIRE_C)   f |= (1 << 2);   // high temp
  if (mq2 > MQ2_GAS_TH)                 f |= (1 << 3);   // gas/smoke
  if (flame < FLAME_TH)                  f |= (1 << 4);   // fire (LOW = flame)
  if (panic)                             f |= (1 << 5);   // panic button
  if (!isnan(h) && h > HUMID_MAX)        f |= (1 << 6);   // extreme humidity
  return f;
}

String flagsToText(uint8_t f) {
  if (f == 0) return "NORMAL";
  String s = "";
  if (f & (1 << 0)) s += "QUAKE ";
  if (f & (1 << 1)) s += "FLOOD ";
  if (f & (1 << 2)) s += "HIGH_TEMP ";
  if (f & (1 << 3)) s += "GAS ";
  if (f & (1 << 4)) s += "FIRE ";
  if (f & (1 << 5)) s += "PANIC ";
  if (f & (1 << 6)) s += "HIGH_HUM ";
  return s;
}

// ===================== SERIAL PRINT (calibration helper) =====================
void printSensorReadings(float t, float h, int water, int mq2,
                          int flame, int vib, bool panic,
                          uint8_t flags, float battV, String battS) {
  Serial.println(F("─────────────────────────────────────────"));
  Serial.print(F("[TIME]    ")); Serial.println(isoTimeNow());
  Serial.println(F(""));

  // DHT22
  Serial.print(F("[DHT22]   Temp = "));
  if (isnan(t)) Serial.print(F("READ ERROR"));
  else { Serial.print(t, 1); Serial.print(F(" °C")); }
  Serial.print(F("   |   Humidity = "));
  if (isnan(h)) Serial.println(F("READ ERROR"));
  else { Serial.print(h, 1); Serial.println(F(" %")); }

  // Water
  Serial.print(F("[WATER]   ADC = ")); Serial.print(water);
  Serial.print(F("   Threshold = ")); Serial.print(WATER_TH);
  Serial.println(water > WATER_TH ? F("  ⚠ FLOOD DETECTED") : F("  OK"));

  // MQ-2
  Serial.print(F("[MQ-2]    ADC = ")); Serial.print(mq2);
  Serial.print(F("   Threshold = ")); Serial.print(MQ2_GAS_TH);
  Serial.println(mq2 > MQ2_GAS_TH ? F("  ⚠ GAS DETECTED") : F("  OK"));

  // Flame
  Serial.print(F("[FLAME]   ADC = ")); Serial.print(flame);
  Serial.print(F("   Threshold = ")); Serial.print(FLAME_TH);
  Serial.println(flame < FLAME_TH ? F("  ⚠ FIRE DETECTED") : F("  OK"));

  // Vibration
  Serial.print(F("[VIB]     DO  = ")); Serial.print(vib);
  Serial.println(vib == HIGH ? F("  ⚠ VIBRATION") : F("  OK"));

  // Panic
  Serial.print(F("[PANIC]   BTN = "));
  Serial.println(panic ? F("PRESSED ⚠") : F("Not pressed"));

  // Battery
  Serial.print(F("[BATT]    Voltage = ")); Serial.print(battV, 2);
  Serial.print(F(" V   Status = ")); Serial.println(battS);

  // SD status
  Serial.print(F("[SD]      ")); Serial.println(sdAvailable ? F("Logging") : F("NOT available"));

  // Hazard summary
  Serial.println(F(""));
  if (flags == 0) {
    Serial.println(F("✅ STATUS: NORMAL — all sensors OK"));
  } else {
    Serial.print(F("🚨 HAZARD: ")); Serial.println(flagsToText(flags));
  }
  Serial.println(F("─────────────────────────────────────────"));
}

// ===================== SETUP =====================
void setup() {
  wdt_disable();

  // Pin modes
  pinMode(PIN_VIB,        INPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  pinMode(PIN_LED_R,      OUTPUT);
  pinMode(PIN_LED_G,      OUTPUT);
  pinMode(PIN_LED_B,      OUTPUT);
  pinMode(PIN_BTN_PANIC,  INPUT_PULLUP);

  setLED(false, true, false);   // green = starting

  Serial.begin(115200);
  Wire.begin();
  dht.begin();

  Serial.println(F(""));
  Serial.println(F("============================================="));
  Serial.println(F("  Disaster BlackBox — SENSOR TEST MODE"));
  Serial.println(F("  No EEPROM | No GSM | No ESP32"));
  Serial.println(F("============================================="));

  // RTC
  if (!rtc.begin()) {
    Serial.println(F("[RTC]  FAIL — check A4/A5 wiring"));
    rtcAvailable = false;
  } else {
    rtcAvailable = true;
    if (rtc.lostPower()) {
      Serial.println(F("[RTC]  Lost power — setting to compile time"));
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println(F("[RTC]  OK"));
  }

  // SD
  sdInit();

  // Boot sequence: blink green 3 times + beep
  blinkLED(false, true, false, 3);
  buzzerBeep(150);

  Serial.println(F(""));
  Serial.println(F("📡 Reading sensors every 2 seconds..."));
  Serial.println(F("  Tip: adjust THRESHOLD values at the top"));
  Serial.println(F("       until detection matches real world."));
  Serial.println(F(""));

  watchdog_enable();
}

// ===================== LOOP =====================
void loop() {
  wdt_reset();

  unsigned long now = millis();

  if (now - lastLogMs >= LOG_INTERVAL_MS) {
    lastLogMs = now;

    // ── Read all sensors ──
    float h     = dht.readHumidity();
    float t     = dht.readTemperature();
    int water   = analogRead(PIN_WATER);
    int mq2     = analogRead(PIN_MQ2);
    int flame   = analogRead(PIN_FLAME);
    int vib     = digitalRead(PIN_VIB);
    bool panic  = (digitalRead(PIN_BTN_PANIC) == LOW);
    float battV = readBatteryVoltage();
    String battS = batteryStatus();

    // ── Compute hazards ──
    uint8_t newFlags = computeHazards(t, h, water, mq2, flame, vib, panic);

    // ── Update LED ──
    if      (newFlags == 0)             setLED(false, true,  false);  // green  = normal
    else if (newFlags & (1 << 5))       setLED(true,  false, true);   // magenta= panic
    else                                setLED(true,  false, false);  // red    = hazard

    // ── Buzzer on new hazard ──
    if (newFlags != 0 && newFlags != hazardFlags) {
      buzzerBeep(300);
    }
    hazardFlags = newFlags;

    // ── Print to Serial Monitor ──
    printSensorReadings(t, h, water, mq2, flame, vib, panic,
                        newFlags, battV, battS);

    // ── Log to SD (if available) ──
    if (sdAvailable && logFile) {
      String ts  = isoTimeNow();
      String row = ts + "," +
                   String(isnan(t) ? 0.0 : t, 1) + "," +
                   String(isnan(h) ? 0.0 : h, 1) + "," +
                   String(water) + "," +
                   String(mq2)   + "," +
                   String(flame) + "," +
                   String(vib)   + "," +
                   String(panic ? 1 : 0) + "," +
                   String(newFlags) + "," +
                   String(battV, 2) + "," +
                   battS;
      logFile.println(row);
      logFile.flush();
    }
  }
}
