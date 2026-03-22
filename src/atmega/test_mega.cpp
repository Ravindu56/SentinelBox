/*
 * Mega/ATmega328P Node — Hardware Test Script
 * Tests: DHT22, Water, MQ2, Flame, Vibration, SD, RTC, GSM, ESP32 link
 * Flash with: pio run -e mega_test -t upload
 */
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include "../../include/config.h"

DHT            dht(PIN_DHT, DHT22);
RTC_DS3231     rtc;
SoftwareSerial sim800(SIM_RX, SIM_TX);

struct TestResult {
  bool dht   = false;
  bool sd    = false;
  bool rtc   = false;
  bool gsm   = false;
  bool uart0 = false;
} tr;

// ─────────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────────
bool simCmd(const char* cmd, const char* expect, uint16_t ms = 2000) {
  sim800.println(cmd);
  unsigned long t0 = millis();
  String r = "";
  while (millis() - t0 < ms) {
    while (sim800.available()) r += (char)sim800.read();
  }
  return r.indexOf(expect) >= 0;
}

// ─────────────────────────────────────────────────────────────────────
// TEST 1: DHT22
// ─────────────────────────────────────────────────────────────────────
void test_dht() {
  Serial.println(F("\n[T1] DHT22 (D4) ────────────────────"));
  dht.begin();
  delay(2000);
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    tr.dht = true;
    Serial.print(F("     PASS — Temp: ")); Serial.print(t, 1);
    Serial.print(F(" C  Hum: "));  Serial.print(h, 1);
    Serial.println(F(" %"));
  } else {
    Serial.println(F("     FAIL — Check 10k pull-up and D4 wiring"));
  }
}

// ─────────────────────────────────────────────────────────────────────
// TEST 2: Analog Sensors
// ─────────────────────────────────────────────────────────────────────
void test_analog() {
  Serial.println(F("\n[T2] Analog Sensors ────────────────"));
  int water = analogRead(PIN_WATER);
  int mq2   = analogRead(PIN_MQ2);
  int flame = analogRead(PIN_FLAME);
  int battR = analogRead(PIN_BATTERY);
  float battV = battR * (5.0f / 1023.0f) * ((33.0f + 10.0f) / 10.0f);

  Serial.print(F("     Water  A0: ")); Serial.print(water);
  Serial.println(water > 100 ? F("  [WET]") : F("  [DRY]"));

  Serial.print(F("     MQ-2   A1: ")); Serial.print(mq2);
  Serial.println(mq2 > MQ2_GAS_TH ? F("  [ALERT]") : F("  [OK]"));

  Serial.print(F("     Flame  A2: ")); Serial.print(flame);
  Serial.println(flame < FLAME_TH ? F("  [FIRE!]") : F("  [OK]"));

  Serial.print(F("     Batt   A6: ")); Serial.print(battR);
  Serial.print(F("  → ")); Serial.print(battV, 2);
  Serial.println(F(" V"));
}

// ─────────────────────────────────────────────────────────────────────
// TEST 3: Digital Sensors
// ─────────────────────────────────────────────────────────────────────
void test_digital() {
  Serial.println(F("\n[T3] Digital Sensors ───────────────"));
  pinMode(PIN_VIB,       INPUT);
  pinMode(PIN_BTN_PANIC, INPUT_PULLUP);

  int vib   = digitalRead(PIN_VIB);
  int panic = digitalRead(PIN_BTN_PANIC);

  Serial.print(F("     Vibration D2: "));
  Serial.println(vib == HIGH ? F("ACTIVE (vibrating)") : F("IDLE"));

  Serial.print(F("     Panic BTN A3: "));
  Serial.println(panic == LOW ? F("PRESSED") : F("Released"));
  Serial.println(F("     (tap the vibration sensor to verify, press panic button)"));
}

// ─────────────────────────────────────────────────────────────────────
// TEST 4: SD Card
// ─────────────────────────────────────────────────────────────────────
void test_sd() {
  Serial.println(F("\n[T4] SD Card (SPI D10/11/12/13) ───"));
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("     FAIL — SD.begin() failed (check CS=D10, SPI wiring)"));
    return;
  }
  File f = SD.open("test.txt", FILE_WRITE);
  if (!f) {
    Serial.println(F("     FAIL — Cannot open file for write"));
    return;
  }
  f.println("BlackBox SD test OK");
  f.close();

  f = SD.open("test.txt");
  if (f) {
    String line = f.readStringUntil('\n');
    f.close();
    SD.remove("test.txt");
    tr.sd = true;
    Serial.print(F("     PASS — Read back: "));
    Serial.println(line);
  } else {
    Serial.println(F("     FAIL — Cannot read back file"));
  }
}

// ─────────────────────────────────────────────────────────────────────
// TEST 5: DS3231 RTC
// ─────────────────────────────────────────────────────────────────────
void test_rtc() {
  Serial.println(F("\n[T5] DS3231 RTC (I2C A4/A5) ───────"));
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println(F("     FAIL — RTC not found on I2C (check SDA=A4, SCL=A5)"));
    return;
  }
  if (rtc.lostPower()) {
    Serial.println(F("     WARNING — RTC lost power, setting compile time"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  DateTime now = rtc.now();
  char buf[22];
  snprintf(buf, sizeof(buf), "     %04d-%02d-%02d %02d:%02d:%02d",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second());
  tr.rtc = true;
  Serial.print(F("     PASS — Time: "));
  Serial.println(buf);
}

// ─────────────────────────────────────────────────────────────────────
// TEST 6: SIM800L GSM
// ─────────────────────────────────────────────────────────────────────
void test_gsm() {
  Serial.println(F("\n[T6] SIM800L GSM (D2/D3) ───────────"));
  sim800.begin(9600);
  delay(1000);
  bool at_ok = simCmd("AT", "OK", 2000);
  if (!at_ok) {
    // try other baud rates
    sim800.begin(115200); delay(500);
    at_ok = simCmd("AT", "OK", 1500);
    if (!at_ok) {
      sim800.begin(57600); delay(500);
      at_ok = simCmd("AT", "OK", 1500);
    }
  }
  if (!at_ok) {
    Serial.println(F("     FAIL — No AT response (check D2/D3, voltage divider, power)"));
    return;
  }

  simCmd("ATE0", "OK");
  bool csq_ok = simCmd("AT+CSQ", "+CSQ", 2000);
  bool creg_ok = simCmd("AT+CREG?", "+CREG", 2000);
  tr.gsm = true;

  // Read signal quality raw
  sim800.println("AT+CSQ");
  delay(1000);
  String resp = "";
  while (sim800.available()) resp += (char)sim800.read();

  Serial.println(F("     PASS — SIM800L responding"));
  Serial.print(F("     Signal: ")); Serial.println(resp);
  if (!csq_ok)  Serial.println(F("     WARNING — No signal quality response"));
  if (!creg_ok) Serial.println(F("     WARNING — No network registration response"));
}

// ─────────────────────────────────────────────────────────────────────
// TEST 7: ESP32 UART Link (D0/D1)
// ─────────────────────────────────────────────────────────────────────
void test_esp32_link() {
  Serial.println(F("\n[T7] ESP32 Link (D0/D1 = Serial) ──"));
  Serial.println(F("     Sending PING to ESP32..."));
  Serial.println(F("     (requires ESP32 to be running with UnoLink active)"));

  // D0/D1 IS Serial on Mega — we use the same Serial port
  // but we print a raw PING and watch for PONG echoed back
  // For this test, use Serial1 (D18/D19 on Mega) as alternative below
  Serial.println("PING");   // ESP32 should reply PONG
  delay(1000);
  // Can't easily self-read on Serial — instruct user
  tr.uart0 = true;
  Serial.println(F("     Manual check: watch ESP32 monitor for [UART1-RX] PING"));
  Serial.println(F("     and watch this monitor for PONG reply from ESP32"));
}

// ─────────────────────────────────────────────────────────────────────
// LED & BUZZER TEST
// ─────────────────────────────────────────────────────────────────────
void test_outputs() {
  Serial.println(F("\n[T8] LED + Buzzer ──────────────────"));
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.println(F("     RED on 500ms"));
  digitalWrite(PIN_LED_R, HIGH); delay(500); digitalWrite(PIN_LED_R, LOW);
  Serial.println(F("     GREEN on 500ms"));
  digitalWrite(PIN_LED_G, HIGH); delay(500); digitalWrite(PIN_LED_G, LOW);
  Serial.println(F("     BLUE on 500ms"));
  digitalWrite(PIN_LED_B, HIGH); delay(500); digitalWrite(PIN_LED_B, LOW);
  Serial.println(F("     BUZZER beep 200ms"));
  digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW);
  Serial.println(F("     PASS — verify visually/audibly"));
}

// ─────────────────────────────────────────────────────────────────────
// SUMMARY
// ─────────────────────────────────────────────────────────────────────
void print_summary() {
  Serial.println(F("\n════════════════════════════════════"));
  Serial.println(F("  MEGA TEST SUMMARY"));
  Serial.println(F("════════════════════════════════════"));
  Serial.print(F("  [T1] DHT22        : ")); Serial.println(tr.dht  ? F("PASS ✓") : F("FAIL ✗"));
  Serial.println(        F("  [T2] Analog Sensors: see values above"));
  Serial.println(        F("  [T3] Digital Sensors: see values above"));
  Serial.print(F("  [T4] SD Card      : ")); Serial.println(tr.sd   ? F("PASS ✓") : F("FAIL ✗"));
  Serial.print(F("  [T5] DS3231 RTC   : ")); Serial.println(tr.rtc  ? F("PASS ✓") : F("FAIL ✗"));
  Serial.print(F("  [T6] SIM800L GSM  : ")); Serial.println(tr.gsm  ? F("PASS ✓") : F("FAIL ✗"));
  Serial.println(        F("  [T7] ESP32 Link    : manual verify"));
  Serial.println(        F("  [T8] LED + Buzzer  : manual verify"));
  Serial.println(F("════════════════════════════════════\n"));
}

// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n╔══════════════════════════════════╗"));
  Serial.println(F("║  Mega BlackBox Hardware Test     ║"));
  Serial.println(F("╚══════════════════════════════════╝"));

  test_dht();
  test_analog();
  test_digital();
  test_sd();
  test_rtc();
  test_gsm();
  test_outputs();
  test_esp32_link();
  print_summary();
}

void loop() {
  // Live sensor stream every 2s
  static unsigned long last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    Serial.printf("[LIVE] T:%.1fC H:%.1f%% W:%d G:%d F:%d V:%d PAN:%d\n",
      t, h,
      analogRead(PIN_WATER),
      analogRead(PIN_MQ2),
      analogRead(PIN_FLAME),
      digitalRead(PIN_VIB),
      !digitalRead(PIN_BTN_PANIC)
    );
  }
}
