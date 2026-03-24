// =====================================================================
// config.h — Shared configuration for ATmega + ESP32
// Edit this file ONLY; all modules #include it.
// =====================================================================
#pragma once

// ── Phone numbers ─────────────────────────────────────────────────
#define SMS_NUMBER_1   "+94766402690"
#define SMS_NUMBER_2   "+94771976289"

// ── WiFi (ESP32) ──────────────────────────────────────────────────
#define WIFI_SSID      "Dialog 4G 720"
#define WIFI_PASS      "82601557"
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
#define SD_CS_PIN_MEGA  53
#define SD_CS_PIN_UNO   10    // Uno SD CS pin (also used for SIM800L on Mega)
#define PIN_BTN_PANIC   A3
#define PIN_WATER       A0
#define PIN_MQ2         A1
#define PIN_FLAME       A2
#define PIN_BATTERY     A4    // voltage divider: 33k/10k → battery+
#define PIN_TP_CHRG     A5  
#define SD_CS_PIN     SD_CS_PIN_UNO

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

// ── Power State Machine ────────────────────────────────────────────
// ATmega D2 → ESP32 GPIO2: HIGH pulse wakes ESP32 from deep sleep
#define PIN_ESP_WAKE        2     // ATmega D2 → ESP32 EXT0 wake pin
#define BATT_IDLE_SLEEP_MS  30000UL   // 30s sleep cycle on battery
#define BATT_CRIT_SLEEP_MS  300000UL  // 5 min sleep cycle, critically low
#define BATT_CRITICAL_V     3.3f      // below this = critical battery
#define ESP_WAKE_PULSE_MS   50        // ms HIGH pulse to wake ESP32

// Power mode enum values stored in SRAM (not EEPROM)
#define PWR_MAINS_NORMAL    0
#define PWR_BATTERY_IDLE    1
#define PWR_EMERGENCY       2
#define PWR_CRITICAL_BATT   3

// New UART protocol frames for power coordination
#define PROTO_SLEEP  "SLEEP"   // ATmega → ESP32: go to deep sleep
#define PROTO_WAKE   "WAKE"    // ATmega → ESP32: wake up  
#define PROTO_PWR    "PWR"     // ATmega → ESP32: power state notification
#define PROTO_ACK    "ACK"     // ESP32 → ATmega: acknowledgment
