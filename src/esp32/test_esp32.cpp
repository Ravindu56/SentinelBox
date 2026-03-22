/*
 * ESP32 Node — Hardware Test Script
 * Tests: WiFi, UART1 (Mega link), UART2 (GPS), MQTT
 * Flash with: pio run -e esp32_test -t upload
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include "../../include/config.h"

HardwareSerial UnoSerial(1);  // UART1: GPIO4=RX, GPIO5=TX
HardwareSerial GPSSerial(2);  // UART2: GPIO16=RX, GPIO17=TX

TinyGPSPlus gps;
WiFiClient  espClient;
PubSubClient mqtt(espClient);

// ── Test result tracker ───────────────────────────────────────────────
struct TestResult {
  bool wifi   = false;
  bool uart1  = false;
  bool gps    = false;
  bool mqtt   = false;
} tr;

// ─────────────────────────────────────────────────────────────────────
// TEST 1: WiFi
// ─────────────────────────────────────────────────────────────────────
void test_wifi() {
  Serial.println(F("\n[T1] WiFi ─────────────────────────"));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("     Connecting"));
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(300); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    tr.wifi = true;
    Serial.println();
    Serial.print(F("     PASS — IP: "));
    Serial.println(WiFi.localIP().toString());
  } else {
    Serial.println(F("\n     FAIL — Check SSID/PASS in config.h"));
  }
}

// ─────────────────────────────────────────────────────────────────────
// TEST 2: UART1 loopback (Mega link)
// ─────────────────────────────────────────────────────────────────────
void test_uart1() {
  Serial.println(F("\n[T2] UART1 Mega Link (GPIO4/5) ─────"));
  Serial.println(F("     Sending PING — bridge GPIO4+GPIO5 with a wire for loopback"));
  Serial.println(F("     OR connect Mega and watch for PONG reply"));

  UnoSerial.println("PING");
  unsigned long t0 = millis();
  String resp = "";
  while (millis() - t0 < 3000) {
    while (UnoSerial.available()) resp += (char)UnoSerial.read();
  }
  resp.trim();
  if (resp.indexOf("PONG") >= 0) {
    tr.uart1 = true;
    Serial.print(F("     PASS — Got: "));
    Serial.println(resp);
  } else if (resp.length() > 0) {
    Serial.print(F("     PARTIAL — Got unexpected: "));
    Serial.println(resp);
  } else {
    Serial.println(F("     FAIL — No response (OK if Mega not connected yet)"));
  }
}

// ─────────────────────────────────────────────────────────────────────
// TEST 3: GPS UART2
// ─────────────────────────────────────────────────────────────────────
void test_gps() {
  Serial.println(F("\n[T3] GPS UART2 (GPIO16/17) ─────────"));
  Serial.println(F("     Listening for NMEA sentences for 5s..."));
  unsigned long t0 = millis();
  int nmea_count = 0;
  bool got_fix   = false;

  while (millis() - t0 < 5000) {
    while (GPSSerial.available()) {
      char c = (char)GPSSerial.read();
      if (gps.encode(c)) {
        nmea_count++;
        if (gps.location.isValid()) got_fix = true;
      }
      // Print raw NMEA for visibility
      Serial.write(c);
    }
  }

  if (nmea_count > 0) {
    tr.gps = true;
    Serial.println();
    Serial.print(F("     PASS — NMEA sentences decoded: "));
    Serial.println(nmea_count);
    if (got_fix) {
      Serial.print(F("     GPS FIX — Lat: "));
      Serial.print(gps.location.lat(), 6);
      Serial.print(F("  Lon: "));
      Serial.println(gps.location.lng(), 6);
    } else {
      Serial.println(F("     No fix yet (normal indoors — data flow OK)"));
    }
  } else {
    Serial.println(F("\n     FAIL — No NMEA data (check GPS wiring GPIO16/17)"));
  }
}

// ─────────────────────────────────────────────────────────────────────
// TEST 4: MQTT
// ─────────────────────────────────────────────────────────────────────
void test_mqtt() {
  Serial.println(F("\n[T4] MQTT ──────────────────────────"));
  if (!tr.wifi) {
    Serial.println(F("     SKIP — WiFi failed, cannot test MQTT"));
    return;
  }
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  String cid = "bb-test-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print(F("     Connecting to "));
  Serial.print(MQTT_HOST);
  Serial.print(':');
  Serial.println(MQTT_PORT);

  unsigned long t0 = millis();
  while (!mqtt.connected() && millis() - t0 < 8000) {
    mqtt.connect(cid.c_str());
    delay(500);
  }
  if (mqtt.connected()) {
    tr.mqtt = true;
    bool pub = mqtt.publish("blackbox/test", "{\"node\":\"esp32\",\"status\":\"test_ok\"}");
    Serial.print(F("     PASS — Publish: "));
    Serial.println(pub ? F("OK") : F("FAIL"));
    mqtt.disconnect();
  } else {
    Serial.println(F("     FAIL — Cannot reach broker (check internet)"));
  }
}

// ─────────────────────────────────────────────────────────────────────
// SUMMARY
// ─────────────────────────────────────────────────────────────────────
void print_summary() {
  Serial.println(F("\n════════════════════════════════════"));
  Serial.println(F("  ESP32 TEST SUMMARY"));
  Serial.println(F("════════════════════════════════════"));
  Serial.print(F("  [T1] WiFi      : ")); Serial.println(tr.wifi  ? F("PASS ✓") : F("FAIL ✗"));
  Serial.print(F("  [T2] UART1     : ")); Serial.println(tr.uart1 ? F("PASS ✓") : F("FAIL ✗"));
  Serial.print(F("  [T3] GPS UART2 : ")); Serial.println(tr.gps   ? F("PASS ✓") : F("FAIL ✗"));
  Serial.print(F("  [T4] MQTT      : ")); Serial.println(tr.mqtt  ? F("PASS ✓") : F("FAIL ✗"));
  Serial.println(F("════════════════════════════════════\n"));
}

// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  UnoSerial.begin(115200, SERIAL_8N1, ESP_UNO_RX, ESP_UNO_TX);
  GPSSerial.begin(9600,   SERIAL_8N1, ESP_GPS_RX, ESP_GPS_TX);

  Serial.println(F("\n╔══════════════════════════════════╗"));
  Serial.println(F("║  ESP32 BlackBox Hardware Test    ║"));
  Serial.println(F("╚══════════════════════════════════╝"));

  test_wifi();
  test_uart1();
  test_gps();
  test_mqtt();
  print_summary();
}

void loop() {
  // Continuous UART1 monitor — shows raw data arriving from Mega
  while (UnoSerial.available()) {
    Serial.print(F("[UART1-RX] "));
    Serial.println(UnoSerial.readStringUntil('\n'));
  }
  // Continuous GPS raw feed
  while (GPSSerial.available()) {
    char c = (char)GPSSerial.read();
    gps.encode(c);
  }
  if (gps.location.isUpdated()) {
    Serial.printf("[GPS] Lat: %.6f  Lon: %.6f  Sats: %d\n",
      gps.location.lat(), gps.location.lng(),
      gps.satellites.isValid() ? (int)gps.satellites.value() : 0);
  }
}
