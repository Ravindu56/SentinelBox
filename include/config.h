// =====================================================================
// config.h — Shared configuration for ATmega + ESP32
// Edit this file ONLY; all modules #include it.
// =====================================================================
#pragma once

// ── Phone numbers ─────────────────────────────────────────────────
#define SMS_NUMBER_1   "+94XXXXXXXXX"
#define SMS_NUMBER_2   "+94XXXXXXXXX"

// ── WiFi (ESP32) ──────────────────────────────────────────────────
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASS      "YOUR_PASSWORD"
#define AP_SSID        "BlackBox-AP"
#define AP_PASS        "blackbox123"

// ── MQTT ──────────────────────────────────────────────────────────
#define MQTT_HOST      "broker.hivemq.com"
#define MQTT_PORT      1883
#define MQTT_TOPIC_TEL "blackbox/telemetry"
#define MQTT_TOPIC_EVT "blackbox/event"
#define MQTT_TOPIC_CMD "blackbox/cmd"
#define VOLTAGE_DIVIDER_RATIO 4.3f

// ── ATmega pin map ────────────────────────────────────────────────
// These Arduino pin numbers work on BOTH Mega and ATmega328P
// (D4-D13, A0-A3 are identical across both boards)
#define PIN_DHT         4
#define PIN_VIB         5
#define PIN_BUZZER      6
#define PIN_LED_R       7
#define PIN_LED_G       8
#define PIN_LED_B       9
#define SD_CS_PIN_MEGA       53
#define SD_CS_PIN_UNO      10    // Uno SD CS pin (also used for SIM800L on Mega)
#define PIN_BTN_PANIC   A3
#define PIN_WATER       A0
#define PIN_MQ2         A1
#define PIN_FLAME       A2
#define PIN_BATTERY     A6    // voltage divider: 33k/10k → battery+
#define PIN_TP_CHRG     A7  

// ── SIM800L serial ────────────────────────────────────────────────
#ifdef TARGET_MEGA
  // Mega: Hardware UART1 — no SoftwareSerial, no disconnect needed
  #define SIM_USE_SERIAL1
  #define SIM_RX   19    // Mega RX1 ← SIM800L EVB TXD (direct)
  #define SIM_TX   18    // Mega TX1 → 10kΩ/20kΩ divider → EVB RXD
#else
  // ATmega328P production: SoftwareSerial on D2/D3
  #define SIM_RX    2    // 328P RX ← SIM800L TX
  #define SIM_TX    3    // 328P TX → divider → SIM800L RX
#endif

#define SD_CS_PIN       SD_CS_PIN_MEGA  // ATmega TX to SIM800L RX (via divider)

// Note: Mega has A6/A7 as ADC-only — same as ATmega328P TQFP
// For 28-pin DIP ATmega328P, use A4/A5 instead for battery/chrg
// if your PCB doesn't break out A6/A7

// ── ESP32 pin map ─────────────────────────────────────────────────
#define GPS_RX_PIN     16     // ESP32 RX2 ← GPS TX
#define GPS_TX_PIN     17     // ESP32 TX2 → GPS RX (optional)
#define UNO_RX_PIN      4     // ESP32 UART1 RX ← ATmega TX (D1)
#define UNO_TX_PIN      5     // ESP32 UART1 TX → ATmega RX (D0)

// ── Sensor thresholds ─────────────────────────────────────────────
// Tune these in TEST MODE with sensor calibration sketch
#define TEMP_FIRE_C    55.0f
#define HUMID_MAX      95.0f
#define MQ2_GAS_TH     450
#define WATER_TH       400
#define FLAME_TH       400    // LOW = flame present (analog)

// ── Timing (ms) ───────────────────────────────────────────────────
#define LOG_INTERVAL_MS       5000UL
#define SMS_COOLDOWN_MS     300000UL   // 5 min
#define HEARTBEAT_MS       3600000UL   // 1 hr
#define SD_FLUSH_EVERY          10     // flush SD every N rows
#define GPS_SEND_INTERVAL_MS  1000UL

// ── Protocol framing ──────────────────────────────────────────────
#define PROTO_TEL  "TEL"
#define PROTO_EVT  "EVT"
#define PROTO_GPS  "GPS"
#define PROTO_PING "PING"
#define PROTO_PONG "PONG"
#define PROTO_CMD  "CMD"