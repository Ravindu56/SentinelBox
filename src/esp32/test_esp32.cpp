/*
 * ESP32 Full Test: Mega Link + SIM800L GSM + GPS
 * Serial  (RX=GPIO03, TX=GPIO01) ← Mega link + Debug @ 115200
 * Serial1 (RX=GPIO14, TX=GPIO12) ← SIM800L    @ 115200
 * Serial2 (RX=GPIO16, TX=GPIO17) ← GPS        @ 9600
 */
#include <Arduino.h>

// ── Pin definitions ───────────────────────────────────────────────────
#define GPS_RX   16   
#define GPS_TX   17  
#define MEGA_RX 03 // ESP32 receives from Mega TX
#define MEGA_TX 01 // ESP32 sends to Mega RX
#define SIM_RX 14  // ESP32 receives from SIM800L TXD
#define SIM_TX 12  // ESP32 sends to SIM800L RXD (via divider)

// ── Config ────────────────────────────────────────────────────────────
#define TEST_PHONE "94705625156" // ← your number
#define SEND_SMS false           // set true to actually send SMS

// ── Helpers ───────────────────────────────────────────────────────────
void pass(const char *m) {
  Serial.print("  [PASS] ");
  Serial.println(m);
}
void fail(const char *m) {
  Serial.print("  [FAIL] ");
  Serial.println(m);
}
void warn(const char *m) {
  Serial.print("  [WARN] ");
  Serial.println(m);
}
void hdr(const char *m) {
  Serial.println();
  Serial.print("=== ");
  Serial.print(m);
  Serial.println(" ===");
}

// ── GSM AT command helper ─────────────────────────────────────────────
bool atCmd(const char *cmd, const char *expect, uint16_t tMs = 3000) {
  while (Serial1.available())
    Serial1.read(); // flush
  Serial.print("  >> ");
  Serial.println(cmd);
  Serial1.println(cmd);
  unsigned long t0 = millis();
  String resp = "";
  while (millis() - t0 < tMs) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf(expect) >= 0) {
        resp.trim();
        Serial.print("  << ");
        Serial.println(resp);
        return true;
      }
    }
  }
  resp.trim();
  Serial.print("  << ");
  Serial.println(resp.length() > 0 ? resp : "(timeout)");
  return false;
}

// ── GSM Tests ─────────────────────────────────────────────────────────
bool simReady = false;

void test_gsm_autobaud() {
  hdr("GSM Auto-Baud (Serial1)");
  Serial.println("  SIM_RX=GPIO14 <- EVB TXD direct");
  Serial.println("  SIM_TX=GPIO12 -> 10k/20k divider -> EVB RXD");
  Serial.println("  EVB VCC=4.2V buck, GND=common");

  const uint32_t bauds[] = {9600, 19200, 38400, 57600, 115200};
  for (uint8_t i = 0; i < 5; i++) {
    Serial1.end();
    delay(50);
    Serial1.begin(bauds[i], SERIAL_8N1, SIM_RX, SIM_TX);
    delay(300);
    while (Serial1.available())
      Serial1.read();
    Serial1.println("AT");
    unsigned long t0 = millis();
    String r = "";
    while (millis() - t0 < 1500) {
      while (Serial1.available())
        r += (char)Serial1.read();
      if (r.indexOf("OK") >= 0)
        break;
    }
    Serial.print("  Baud ");
    Serial.print(bauds[i]);
    if (r.indexOf("OK") >= 0) {
      Serial.println(" -> OK");
      simReady = true;
      pass("SIM800L responding");
      // lock baud and disable echo
      Serial1.println("ATE0");
      delay(200);
      return;
    }
    Serial.println(" -> no response");
  }
  fail("SIM800L not responding — check power/wiring");
  Serial.println("  Checklist:");
  Serial.println("  1. EVB VCC = 4.2V? (NOT 3.3V from ESP32)");
  Serial.println("  2. EVB LED blinking every 3s?");
  Serial.println("  3. GPIO14 <- EVB TXD direct wire?");
  Serial.println("  4. GPIO12 -> divider junction -> EVB RXD?");
  Serial.println("  5. Common GND: ESP32 + EVB + Mega?");
}

void test_gsm_simcard() {
  hdr("SIM Card");
  if (!simReady) {
    warn("Skipped");
    return;
  }
  atCmd("AT+CPIN?", "READY", 5000) ? pass("SIM ready")
                                   : fail("SIM not ready — check card/PIN");
}

void test_gsm_network() {
  hdr("Network Registration");
  if (!simReady) {
    warn("Skipped");
    return;
  }
  atCmd("AT+CREG?", "CREG", 5000);
  Serial.println("  0,1=home  0,5=roaming  0,2=searching");
}

void test_gsm_signal() {
  hdr("Signal Strength");
  if (!simReady) {
    warn("Skipped");
    return;
  }
  atCmd("AT+CSQ", "CSQ");
  Serial.println("  10-14=OK  15-19=good  20-31=excellent  99=none");
}

void test_gsm_sms() {
  hdr("Send SMS");
  if (!simReady) {
    warn("Skipped");
    return;
  }
#if SEND_SMS
  if (!atCmd("AT+CMGF=1", "OK", 2000)) {
    fail("CMGF failed");
    return;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "AT+CMGS=\"%s\"", TEST_PHONE);
  while (Serial1.available())
    Serial1.read();
  Serial.print("  >> ");
  Serial.println(buf);
  Serial1.println(buf);
  delay(500);
  Serial1.print("BlackBox ESP32+GSM Test OK");
  Serial1.write(26); // Ctrl+Z
  unsigned long t0 = millis();
  String r = "";
  while (millis() - t0 < 12000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      r += c;
      Serial.write(c);
    }
    if (r.indexOf("+CMGS") >= 0) {
      pass("SMS sent!");
      return;
    }
    if (r.indexOf("ERROR") >= 0) {
      fail("ERROR response");
      return;
    }
  }
  fail("Timeout");
#else
  warn("Set SEND_SMS true to enable");
#endif
}

void test_gps() {
  hdr("GPS Module");
  Serial.println("  Testing GPS on Serial2...");
  unsigned long t0 = millis();
  String r = "";
  while (millis() - t0 < 3000) {
    while (Serial2.available()) {
      char c = Serial2.read();
      if (r.length() < 100) r += c;
    }
  }
  if (r.indexOf("$G") >= 0) {
    pass("GPS NMEA data received!");
  } else if (r.length() > 0) {
    warn("Data received, but no $G identifier (wrong baud?)");
  } else {
    fail("No GPS data received");
  }
}

// ── Mega Link State ───────────────────────────────────────────────────
static uint32_t rxCount = 0;
static uint32_t ackCount = 0;
static uint32_t errCount = 0;
static String rxBuf = "";

struct TelFrame {
  unsigned long ts;
  float temp, hum, batt;
  int water, gas, flame, vib;
  uint8_t flags;
  bool valid;
};

TelFrame parseTel(const String &line) {
  TelFrame f = {};
  if (!line.startsWith("TEL,"))
    return f;
  char buf[110];
  line.toCharArray(buf, sizeof(buf));
  char *tok = strtok(buf, ","); // TEL
  tok = strtok(NULL, ",");      // ts
  if (tok)
    f.ts = atol(tok);
  while ((tok = strtok(NULL, ",")) != NULL) {
    if (strncmp(tok, "T:", 2) == 0)
      f.temp = atof(tok + 2);
    else if (strncmp(tok, "H:", 2) == 0)
      f.hum = atof(tok + 2);
    else if (strncmp(tok, "W:", 2) == 0)
      f.water = atoi(tok + 2);
    else if (strncmp(tok, "G:", 2) == 0)
      f.gas = atoi(tok + 2);
    else if (strncmp(tok, "F:", 2) == 0)
      f.flame = atoi(tok + 2);
    else if (strncmp(tok, "V:", 2) == 0)
      f.vib = atoi(tok + 2);
    else if (strncmp(tok, "B:", 2) == 0)
      f.batt = atof(tok + 2);
    else if (strncmp(tok, "FLAGS:", 6) == 0)
      f.flags = (uint8_t)strtol(tok + 6, NULL, 16);
  }
  f.valid = true;
  return f;
}

// ── Setup ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n====================================");
  Serial.println("  ESP32 Full Test: Mega + GSM + GPS");
  Serial.println("  Serial : Mega  RX=GPIO03 TX=GPIO01");
  Serial.println("  Serial1: GSM   RX=GPIO14 TX=GPIO12");
  Serial.println("  Serial2: GPS   RX=GPIO16 TX=GPIO17");
  Serial.println("====================================");

  // Mega link uses UART0 / Serial
  Serial.println("[OK] Serial (Mega link) shared on pins 3/1");

  // GPS link
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  delay(200);
  while (Serial2.available())
    Serial2.read();
  Serial.println("[OK] Serial2 (GPS) ready");

  // GSM tests — run once at boot
  test_gsm_autobaud();
  test_gsm_simcard();
  test_gsm_network();
  test_gsm_signal();
  test_gsm_sms();
  test_gps();

  Serial.println("\n====================================");
  Serial.println("  Boot tests done. Entering loop.");
  Serial.println("  Waiting for TEL frames from Mega...");
  Serial.println("====================================\n");
}

// ── Loop ──────────────────────────────────────────────────────────────
void loop() {
  // ── Receive TEL from Mega ──────────────────────────────────────
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      rxBuf.trim();
      if (rxBuf.length() > 0) {
        rxCount++;
        if (rxBuf.startsWith("TEL,")) {
          TelFrame f = parseTel(rxBuf);
          if (f.valid) {
            Serial.print("[TEL] T=");
            Serial.print(f.temp, 1);
            Serial.print("C H=");
            Serial.print(f.hum, 1);
            Serial.print("% W=");
            Serial.print(f.water);
            Serial.print(" G=");
            Serial.print(f.gas);
            Serial.print(" B=");
            Serial.print(f.batt, 2);
            Serial.print("V FL=0x");
            Serial.println(f.flags, HEX);
            // Send ACK back to Mega
            char ack[30];
            snprintf(ack, sizeof(ack), "ACK,%lu\n", millis());
            Serial.print(ack);
            ackCount++;
            // ── Future: forward to GSM here ──
            // if (simReady && f.flags > 0) gsm_alert(f);
          } else {
            errCount++;
          }
        }
      }
      rxBuf = "";
    } else {
      if (rxBuf.length() < 120)
        rxBuf += c;
    }
  }

  // ── Pass GSM unsolicited messages to USB Serial ────────────────
  while (Serial1.available()) {
    Serial.write(Serial1.read());
  }

  // ── Pass GPS sentences to USB Serial ───────────────────────────
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }

  // ── Stats every 10s ───────────────────────────────────────────
  static unsigned long lastStat = 0;
  if (millis() - lastStat >= 10000) {
    lastStat = millis();
    Serial.print("[STAT] RX:");
    Serial.print(rxCount);
    Serial.print(" ACK:");
    Serial.print(ackCount);
    Serial.print(" ERR:");
    Serial.println(errCount);
  }
}
