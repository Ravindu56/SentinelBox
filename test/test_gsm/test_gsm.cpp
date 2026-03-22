#include <Arduino.h>

// ── SIM800L EVB on Hardware UART1 ────────────────────────────────────
// Mega: TX1=D18 → voltage divider → EVB RXD
//       RX1=D19 ← EVB TXD (direct, no divider needed)
// EVB VCC accepts 5V (onboard regulator)
// NO SoftwareSerial — using Hardware Serial1
#define sim         Serial1
#define SIM_BAUD    9600

#define TEST_PHONE  "+94705625156"
#define SEND_SMS    false

// ── helpers ───────────────────────────────────────────────────────────
void pass(const __FlashStringHelper* m) { Serial.print(F("  [PASS] ")); Serial.println(m); }
void fail(const __FlashStringHelper* m) { Serial.print(F("  [FAIL] ")); Serial.println(m); }
void warn(const __FlashStringHelper* m) { Serial.print(F("  [WARN] ")); Serial.println(m); }
void hdr (const __FlashStringHelper* m) {
    Serial.println();
    Serial.print(F("--- ")); Serial.print(m); Serial.println(F(" ---"));
}

bool atCmd(const __FlashStringHelper* cmd, const char* expect, uint16_t tMs = 3000) {
    while (sim.available()) sim.read();
    Serial.print(F("  >> ")); Serial.println(cmd);
    sim.println(cmd);
    unsigned long t0 = millis();
    String resp = "";
    while (millis() - t0 < tMs) {
        while (sim.available()) resp += (char)sim.read();
        if (resp.indexOf(expect) >= 0) break;
    }
    resp.trim();
    Serial.print(F("  << "));
    resp.length() > 0 ? Serial.println(resp) : Serial.println(F("(no response)"));
    return resp.indexOf(expect) >= 0;
}

// ── tests ─────────────────────────────────────────────────────────────

bool simReady = false;

void test_autobaud() {
    hdr(F("Auto-Baud Detection (Hardware Serial1)"));
    Serial.println(F("  Mega TX1=D18 → 10k/20k divider → EVB RXD"));
    Serial.println(F("  Mega RX1=D19 ← EVB TXD (direct)"));
    Serial.println(F("  EVB VCC=5V from Mega (onboard regulator)"));

    const uint32_t bauds[] = {9600, 19200, 38400, 57600, 115200};
    for (uint8_t i = 0; i < 5; i++) {
        sim.end();
        delay(50);
        sim.begin(bauds[i]);
        delay(200);
        while (sim.available()) sim.read();
        sim.println(F("AT"));
        unsigned long t0 = millis();
        String r = "";
        while (millis() - t0 < 1500) {
            while (sim.available()) r += (char)sim.read();
            if (r.indexOf("OK") >= 0) break;
        }
        Serial.print(F("  Baud ")); Serial.print(bauds[i]);
        if (r.indexOf("OK") >= 0) {
            Serial.println(F(" → OK ✓"));
            simReady = true;
            pass(F("SIM800L responded on Serial1"));
            return;
        }
        Serial.println(F(" → no response"));
    }

    fail(F("No baud worked"));
    Serial.println(F("  >> EVB checklist:"));
    Serial.println(F("     1. LED blinking every 3s? (confirmed ON) ✓"));
    Serial.println(F("     2. D18 → 10kΩ → EVB_RXD, junction → 20kΩ → GND?"));
    Serial.println(F("        (R1=10k series, R2=20k to GND = 3.33V at junction)"));
    Serial.println(F("     3. EVB_TXD → D19 directly (no divider)?"));
    Serial.println(F("     4. Common GND between Mega and EVB?"));
    Serial.println(F("     5. EVB PWRKEY jumper — some EVBs need it shorted at boot"));
}

void test_echo_off() {
    hdr(F("Echo Off"));
    if (!simReady) { warn(F("Skipped")); return; }
    atCmd(F("ATE0"), "OK") ? pass(F("Echo disabled")) : fail(F("ATE0 failed"));
}

void test_sim_present() {
    hdr(F("SIM Card"));
    if (!simReady) { warn(F("Skipped")); return; }
    bool ok = atCmd(F("AT+CPIN?"), "READY", 5000);
    if (ok) {
        pass(F("SIM ready — no PIN lock"));
    } else {
        fail(F("SIM not ready"));
        Serial.println(F("  >> SIM inserted in EVB slot? Gold contacts face down?"));
        Serial.println(F("  >> PIN lock enabled on SIM? Disable via phone settings."));
    }
}

void test_network_reg() {
    hdr(F("Network Registration"));
    if (!simReady) { warn(F("Skipped")); return; }
    atCmd(F("AT+CREG?"), "+CREG:", 5000);
    Serial.println(F("  >> 0,1=home  0,5=roaming  0,2=searching  0,0=not registered"));
    pass(F("CREG queried — check value"));
}

void test_signal() {
    hdr(F("Signal Strength"));
    if (!simReady) { warn(F("Skipped")); return; }
    atCmd(F("AT+CSQ"), "+CSQ:");
    Serial.println(F("  >> 0-9=poor  10-14=OK  15-19=good  20-31=excellent  99=no signal"));
    pass(F("CSQ queried — check value"));
}

void test_operator() {
    hdr(F("Network Operator"));
    if (!simReady) { warn(F("Skipped")); return; }
    atCmd(F("AT+COPS?"), "+COPS:", 5000)
        ? pass(F("Operator received"))
        : fail(F("Not registered to any operator"));
}

void test_evb_voltage() {
    hdr(F("EVB Supply Voltage (AT+CBC)"));
    if (!simReady) { warn(F("Skipped")); return; }
    // +CBC: 0,<charge%>,<mV>  — reports voltage at SIM800L VCC (after EVB regulator)
    bool ok = atCmd(F("AT+CBC"), "+CBC:", 3000);
    if (ok) {
        pass(F("Supply voltage received — should be ~3900-4200mV after EVB regulator"));
    } else {
        warn(F("CBC not responding"));
    }
}

void test_sms_mode() {
    hdr(F("SMS Text Mode"));
    if (!simReady) { warn(F("Skipped")); return; }
    atCmd(F("AT+CMGF=1"), "OK", 2000)
        ? pass(F("Text mode set"))
        : fail(F("CMGF=1 failed"));
}

void test_send_sms() {
    hdr(F("Send Test SMS"));
    if (!simReady) { warn(F("Skipped — not ready")); return; }

#if SEND_SMS
    Serial.print(F("  To: ")); Serial.println(F(TEST_PHONE));
    sim.println(F("AT+CMGF=1"));
    delay(500);
    while (sim.available()) sim.read();

    char buf[28];
    snprintf(buf, sizeof(buf), "AT+CMGS=\"%s\"", TEST_PHONE);
    Serial.print(F("  >> ")); Serial.println(buf);
    sim.println(buf);
    delay(500);

    sim.print(F("BlackBox EVB Test SMS OK"));
    sim.write(26);  // Ctrl+Z

    unsigned long t0 = millis();
    String r = "";
    while (millis() - t0 < 12000) {
        while (sim.available()) { char c = sim.read(); r += c; Serial.write(c); }
        if (r.indexOf("+CMGS:") >= 0) { pass(F("SMS sent!")); return; }
        if (r.indexOf("ERROR")  >= 0) { fail(F("ERROR — check signal/credit")); return; }
    }
    fail(F("Timeout — no +CMGS"));
#else
    warn(F("Skipped — set #define SEND_SMS true to test"));
#endif
}

// ── entry ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println(F("========================================="));
    Serial.println(F("   SIM800L EVB Test — Hardware Serial1"));
    Serial.println(F("   Mega TX1=D18 → divider → EVB RXD"));
    Serial.println(F("   Mega RX1=D19 ← EVB TXD (direct)"));
    Serial.println(F("   EVB VCC=5V (onboard regulator to 4V)"));
    Serial.println(F("========================================="));

    test_autobaud();
    test_echo_off();
    test_sim_present();
    test_network_reg();
    test_signal();
    test_operator();
    test_evb_voltage();
    test_sms_mode();
    test_send_sms();

    Serial.println(F("\n========================================="));
    Serial.println(F("   Done — type AT commands below"));
    Serial.println(F("========================================="));
}

void loop() {
    // AT command passthrough for manual testing
    if (Serial.available()) sim.write(Serial.read());
    if (sim.available())    Serial.write(sim.read());
}
