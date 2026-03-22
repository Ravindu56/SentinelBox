#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// ── Pin config — Mega SPI ─────────────────────────────────────────────
// MOSI = D51, MISO = D50, SCK = D52, CS = D53 (hardware SS on Mega)
// If your module uses D10 as CS, change PIN_SD_CS to 10
#define PIN_SD_CS   53

// ── helpers ───────────────────────────────────────────────────────────
void pass(const __FlashStringHelper* m) { Serial.print(F("  [PASS] ")); Serial.println(m); }
void fail(const __FlashStringHelper* m) { Serial.print(F("  [FAIL] ")); Serial.println(m); }
void warn(const __FlashStringHelper* m) { Serial.print(F("  [WARN] ")); Serial.println(m); }
void hdr (const __FlashStringHelper* m) {
    Serial.println();
    Serial.print(F("--- ")); Serial.print(m); Serial.println(F(" ---"));
}

// ── tests ─────────────────────────────────────────────────────────────

bool sdReady = false;   // gate later tests if init fails

void test_sd_init() {
    hdr(F("SD Card Init"));
    Serial.print(F("  CS pin : D")); Serial.println(PIN_SD_CS);
    Serial.print(F("  MOSI   : D51  MISO: D50  SCK: D52  (Mega hardware SPI)\n"));

    if (!SD.begin(PIN_SD_CS)) {
        fail(F("SD.begin() failed"));
        Serial.println(F("  >> Causes:"));
        Serial.println(F("     - No card inserted"));
        Serial.println(F("     - Card not FAT32 formatted"));
        Serial.println(F("     - Wrong CS pin"));
        Serial.println(F("     - 3.3V module needs level shifter on MOSI/SCK/CS"));
        Serial.println(F("     - SD module power not connected"));
        return;
    }
    sdReady = true;
    pass(F("SD.begin() OK"));
}

void test_sd_card_type() {
    hdr(F("Card Type"));
    if (!sdReady) { warn(F("Skipped — SD not ready")); return; }
    // SD.card is private in arduino-libraries/SD — skip type check
    // If SD.begin() passed, card is present and readable
    pass(F("Card present and initialized"));
}

void test_sd_volume() {
    hdr(F("Volume Info"));
    if (!sdReady) { warn(F("Skipped — SD not ready")); return; }

    // Check root directory is listable
    File root = SD.open(F("/"));
    if (!root) {
        fail(F("Cannot open root directory"));
        return;
    }
    Serial.println(F("  Root contents:"));
    uint8_t count = 0;
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        Serial.print(F("    ")); Serial.println(entry.name());
        entry.close();
        count++;
        if (count >= 10) { Serial.println(F("    ... (truncated)")); break; }
    }
    root.close();
    if (count == 0) Serial.println(F("    (empty — fresh card)"));
    pass(F("Root directory readable"));
}

void test_sd_write() {
    hdr(F("Write Test"));
    if (!sdReady) { warn(F("Skipped — SD not ready")); return; }

    // Remove old test file if exists
    if (SD.exists(F("bbtest.txt"))) SD.remove(F("bbtest.txt"));

    File f = SD.open(F("bbtest.txt"), FILE_WRITE);
    if (!f) {
        fail(F("Could not open bbtest.txt for write"));
        return;
    }

    uint8_t lines = 5;
    for (uint8_t i = 0; i < lines; i++) {
        f.print(F("LINE,"));
        f.print(i);
        f.print(F(",TEST,"));
        f.println(millis());
    }
    f.close();
    pass(F("Wrote 5 lines to bbtest.txt"));
}

void test_sd_read_verify() {
    hdr(F("Read & Verify"));
    if (!sdReady) { warn(F("Skipped — SD not ready")); return; }

    if (!SD.exists(F("bbtest.txt"))) {
        fail(F("bbtest.txt not found — write test may have failed"));
        return;
    }

    File f = SD.open(F("bbtest.txt"), FILE_READ);
    if (!f) {
        fail(F("Could not open bbtest.txt for read"));
        return;
    }

    uint8_t lineCount = 0;
    char buf[48];
    while (f.available()) {
        uint8_t i = 0;
        while (f.available() && i < sizeof(buf) - 1) {
            char c = f.read();
            if (c == '\n') break;
            buf[i++] = c;
        }
        buf[i] = '\0';
        Serial.print(F("  Line ")); Serial.print(lineCount); Serial.print(F(": ")); Serial.println(buf);
        lineCount++;
    }
    f.close();

    Serial.print(F("  Lines read: ")); Serial.println(lineCount);
    lineCount == 5 ? pass(F("Read 5 lines OK")) : fail(F("Line count mismatch"));
}

void test_sd_append() {
    hdr(F("Append Test"));
    if (!sdReady) { warn(F("Skipped — SD not ready")); return; }

    File f = SD.open(F("bbtest.txt"), FILE_WRITE);  // FILE_WRITE appends
    if (!f) { fail(F("Could not open for append")); return; }
    f.println(F("APPEND,LINE,OK"));
    f.close();
    pass(F("Appended line OK"));
}

void test_sd_delete() {
    hdr(F("Delete Test"));
    if (!sdReady) { warn(F("Skipped — SD not ready")); return; }

    SD.remove(F("bbtest.txt"));
    !SD.exists(F("bbtest.txt")) ? pass(F("File deleted OK")) : fail(F("File still exists after delete"));
}

void test_sd_repeated_open() {
    hdr(F("Stress: 20x Open/Write/Close"));
    if (!sdReady) { warn(F("Skipped — SD not ready")); return; }

    if (SD.exists(F("stress.csv"))) SD.remove(F("stress.csv"));

    uint8_t ok = 0;
    for (uint8_t i = 0; i < 20; i++) {
        File f = SD.open(F("stress.csv"), FILE_WRITE);
        if (f) {
            f.print(i); f.print(','); f.println(millis());
            f.close();
            ok++;
        }
    }
    Serial.print(F("  Passed: ")); Serial.print(ok); Serial.println(F("/20"));
    SD.remove(F("stress.csv"));
    ok == 20 ? pass(F("All 20 open/write/close OK")) : fail(F("Some iterations failed"));
}

// ── entry ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println(F("========================================="));
    Serial.println(F("   SD Card Module Test"));
    Serial.println(F("   Mega: MOSI=D51 MISO=D50 SCK=D52"));
    Serial.print  (F("   CS  : D")); Serial.println(PIN_SD_CS);
    Serial.println(F("========================================="));

    test_sd_init();
    test_sd_card_type();
    test_sd_volume();
    test_sd_write();
    test_sd_read_verify();
    test_sd_append();
    test_sd_delete();
    test_sd_repeated_open();

    Serial.println(F("\n========================================="));
    Serial.println(F("   Done — check results above"));
    Serial.println(F("========================================="));
}

void loop() {}
