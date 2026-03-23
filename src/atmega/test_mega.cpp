/*
 * Mega → ESP32 Serial Link Test
 * Mega Serial2 (TX2=D16, RX2=D17) at 115200 baud
 * Sends TEL frames every 2s, prints ACK responses
 * Flash: pio run -e mega_test -t upload
 */
#include <Arduino.h>

// ── Helpers ──────────────────────────────────────────────────────────
void pass(const __FlashStringHelper* m) { Serial.print(F("  [PASS] ")); Serial.println(m); }
void fail(const __FlashStringHelper* m) { Serial.print(F("  [FAIL] ")); Serial.println(m); }
void hdr (const __FlashStringHelper* m) {
  Serial.println();
  Serial.print(F("=== ")); Serial.print(m); Serial.println(F(" ==="));
}

// ── Test 1: Loopback — D16 wired to D17 directly ─────────────────────
void test_loopback() {
  hdr(F("Serial2 Loopback (D16-D17 shorted)"));
  Serial.println(F("  Short D16 to D17, then press Enter"));
  Serial.println(F("  (skip with 's' if ESP32 already wired)"));

  // Wait for user to confirm
  while (!Serial.available()) {}
  char c = Serial.read();
  if (c == 's' || c == 'S') {
    Serial.println(F("  Skipped"));
    return;
  }

  Serial2.begin(115200);
  delay(100);
  while (Serial2.available()) Serial2.read(); // flush

  const char* msg = "LOOPBACK_OK\n";
  Serial2.print(msg);
  unsigned long t0 = millis();
  String resp = "";
  while (millis() - t0 < 500) {
    while (Serial2.available()) resp += (char)Serial2.read();
  }
  if (resp.indexOf("LOOPBACK_OK") >= 0) {
    pass(F("Serial2 TX/RX hardware OK"));
  } else {
    fail(F("No loopback echo — check D16/D17 wiring"));
  }
}

// ── Test 2: Send TEL frames to ESP32, wait for ACK ───────────────────
static uint32_t frameCount = 0;

void send_tel_frame() {
  // Fake sensor values — real sensors are unplugged
  float  t    = 28.5f;
  float  h    = 72.0f;
  int    w    = 120;
  int    g    = 310;
  int    f    = 890;
  int    vib  = 0;
  float  batt = 4.12f;
  uint8_t flags = 0x00;

  char buf[100];
  snprintf(buf, sizeof(buf),
    "TEL,%lu,T:%.1f,H:%.1f,W:%d,G:%d,F:%d,V:%d,B:%.2f,FLAGS:%02X\n",
    millis(), t, h, w, g, f, vib, batt, flags);

  Serial2.print(buf);
  Serial.print(F("  >> SENT: ")); Serial.print(buf);
  frameCount++;
}

bool wait_ack(uint16_t timeoutMs = 1000) {
  unsigned long t0 = millis();
  String resp = "";
  while (millis() - t0 < timeoutMs) {
    while (Serial2.available()) {
      char c = Serial2.read();
      resp += c;
      if (c == '\n') {
        resp.trim();
        Serial.print(F("  << RECV: ")); Serial.println(resp);
        return resp.startsWith("ACK");
      }
    }
  }
  Serial.println(F("  << RECV: (timeout)"));
  return false;
}

void test_link() {
  hdr(F("Mega → ESP32 Serial2 Link (115200)"));
  Serial.println(F("  D16(TX2) ─[10k/20k divider]─ ESP32 GPIO16(RX2)"));
  Serial.println(F("  D17(RX2) ──────────────────── ESP32 GPIO17(TX2)"));
  Serial.println(F("  Sending 5 TEL frames, expecting ACK each...\n"));

  Serial2.begin(115200);
  delay(500); // let ESP32 settle

  uint8_t passed = 0;
  for (uint8_t i = 0; i < 5; i++) {
    send_tel_frame();
    if (wait_ack(1500)) {
      passed++;
    }
    delay(200);
  }

  Serial.println();
  if (passed == 5) {
    pass(F("All 5 ACKs received — link fully working"));
  } else if (passed > 0) {
    Serial.print(F("  [WARN] ")); Serial.print(passed);
    Serial.println(F("/5 ACKs — intermittent, check voltage divider"));
  } else {
    fail(F("0/5 ACKs — ESP32 not responding"));
    Serial.println(F("  Checklist:"));
    Serial.println(F("  1. ESP32 flashed with test_esp32.cpp?"));
    Serial.println(F("  2. D16 → 10kΩ → junction → 20kΩ → GND, junction → ESP32 GPIO16"));
    Serial.println(F("  3. Common GND between Mega and ESP32?"));
    Serial.println(F("  4. ESP32 powered independently (not via Mega 3.3V)?"));
    Serial.println(F("  5. Measure D16 HIGH = ~5V, junction = ~3.3V?"));
  }
}

// ── Test 3: Live monitor — passthrough for manual inspection ─────────
void live_monitor() {
  hdr(F("Live Monitor (Ctrl+C to stop)"));
  Serial.println(F("  All bytes from Serial2 echoed to Serial0"));
  Serial.println(F("  Type in Serial Monitor to send to ESP32\n"));

  unsigned long start = millis();
  while (millis() - start < 15000UL) { // 15 second window
    if (Serial.available())  Serial2.write(Serial.read());
    if (Serial2.available()) Serial.write(Serial2.read());
  }
  Serial.println(F("\n  Monitor window closed"));
}

// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n===================================="));
  Serial.println(F("  Mega ↔ ESP32 Serial Link Test"));
  Serial.println(F("  Serial2: TX2=D16, RX2=D17, 115200"));
  Serial.println(F("===================================="));

  test_loopback();
  test_link();
  live_monitor();

  Serial.println(F("\n===================================="));
  Serial.println(F("  Done. Switching to continuous TX"));
  Serial.println(F("====================================\n"));
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    send_tel_frame();
    wait_ack(500);
  }
  // pass-through for manual debugging
  if (Serial.available())  Serial2.write(Serial.read());
  if (Serial2.available()) {
    char c = Serial2.read();
    Serial.write(c);
  }
}
