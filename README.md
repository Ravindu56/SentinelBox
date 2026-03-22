# SentinelBox 🛡️
### Disaster-Proof Household Monitoring & Alert System

> An embedded systems project built for real-world disaster resilience — inspired by the impact of **Cyclone Ditwah (2025)** on Sri Lankan households.

[![PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-orange)](https://platformio.org/)
[![AVR](https://img.shields.io/badge/MCU-ATmega328P-blue)](https://www.microchip.com/en-us/product/ATmega328P)
[![ESP32](https://img.shields.io/badge/Utility%20Node-ESP32-green)](https://www.espressif.com/en/products/socs/esp32)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![EC6020](https://img.shields.io/badge/Course-EC6020%20Embedded%20Systems-purple)](https://www.jfn.ac.lk)

---

## 📌 What is SentinelBox?

SentinelBox is a low-cost, battery-backed embedded system that continuously monitors a household for environmental hazards — floods, fires, gas leaks, earthquakes — and responds automatically with SMS alerts, local alarms, data logging, and a live web dashboard.

It is designed to **survive a disaster** (not just detect one): the device runs on mains power normally, switches to battery when electricity fails, and keeps logging and alerting throughout.

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     POWER ARCHITECTURE                      │
│  Mains 5V ──► Bus A: ATmega328P + All Sensors (150mA)      │
│             ──► Bus B: ESP32 + SIM800L (2A peak)           │
│  LiPo 3.7V ──► TP4056 ──► Failover on mains loss           │
└─────────────────────────────────────────────────────────────┘

┌──────────────────────┐        Serial (UART)       ┌──────────────────────┐
│   ATmega328P (Core)  │ ◄─────────────────────────► │   ESP32 (Utility)    │
│                      │                             │                      │
│  • DHT22 Temp/Humid  │                             │  • NEO-6M GPS        │
│  • Water Level       │                             │  • SIM800L GSM/GPRS  │
│  • MQ-2 Gas/Smoke    │                             │  • WiFi Dashboard    │
│  • Flame Sensor      │                             │  • MQTT Telemetry    │
│  • SW-420 Vibration  │                             │  • Async Web Server  │
│  • Panic Button      │                             └──────────────────────┘
│  • DS3231 RTC        │
│  • SD Card Logger    │
│  • RGB LED + Buzzer  │
│  • Battery Monitor   │
└──────────────────────┘
```

---

## ✨ Features

- 🌡️ **Multi-sensor fusion** — Temperature, humidity, water level, gas/smoke, flame, vibration, panic button
- 📡 **SMS alerts** via SIM800L — immediate notification on critical hazard detection
- 🌍 **GPS location** embedded in alerts — Google Maps link in every SMS
- 💾 **SD card logging** — timestamped CSV rows, survives power loss
- ⏰ **DS3231 RTC** — accurate timestamps even without internet
- 🔋 **Battery failover** — TP4056 + LiPo, automatic mains/battery switching
- 📊 **Live web dashboard** — ESP32 async server with SSE real-time push
- 📬 **MQTT telemetry** — publishes to HiveMQ broker
- 🐕 **Watchdog + sleep modes** — reliable recovery, ultra-low idle power
- 🔧 **Modular codebase** — test each module independently before integration

---

## 🌿 Branch Structure

| Branch | Purpose |
|--------|---------|
| `main` | Stable, production-ready releases |
| `dev` | Active development, integration testing |
| `atmega` | ATmega328P / Mega2560 core controller firmware |
| `esp32` | ESP32 utility node firmware |

> **Development boards:** Testing is done on **Arduino Mega 2560** (same pin map as ATmega328P for D4–D13, A0–A3). Production target is the bare **ATmega328P-PU** chip.

---

## 📁 Project Structure

```
sentinelbox/
├── include/
│   └── config.h              ← Single config file — edit only this
├── src/
│   ├── atmega/
│   │   ├── main.cpp          ← Boot-once, millis() scheduler
│   │   ├── sensors.cpp/.h    ← DHT22, water, MQ-2, flame, vibration, panic
│   │   ├── hazards.cpp/.h    ← Bitmask detection + text output
│   │   ├── rtc_time.cpp/.h   ← DS3231 timestamps
│   │   ├── storage.cpp/.h    ← SD card CSV logging
│   │   ├── gsm.cpp/.h        ← SIM800L SMS (on ESP32 in production)
│   │   ├── leds.cpp/.h       ← RGB LED + async buzzer state machine
│   │   ├── comms.cpp/.h      ← ATmega ↔ ESP32 serial protocol
│   │   └── power.cpp/.h      ← Battery ADC, TP4056, WDT, sleep
│   └── esp32/
│       ├── gps_parser.cpp/.h ← NEO-6M on UART2 via TinyGPSPlus
│       ├── uno_link.cpp/.h   ← ATmega serial link on UART1
│       ├── wifi_mqtt.cpp/.h  ← WiFi + AP fallback + MQTT backoff
│       └── web_server.cpp/.h ← Async dashboard with SSE push
├── test/
│   ├── test_sensors/         ← Sensor calibration tests
│   ├── test_rtc/             ← RTC read/write tests
│   ├── test_sd/              ← SD card write/read tests
│   └── test_gsm/             ← SIM800L AT command tests
└── platformio.ini            ← Multi-environment build config
```

---

## ⚙️ PlatformIO Environments

```ini
[env:mega]          → Arduino Mega 2560 (testing)
[env:atmega328p]    → Bare ATmega328P chip (production)
[env:esp32]         → ESP32 WROOM utility node
```

```bash
# Upload to Mega for testing
pio run -e mega -t upload

# Upload to bare ATmega328P via ISP programmer
pio run -e atmega328p -t upload

# Upload to ESP32
pio run -e esp32 -t upload

# Run a specific test module
pio test -e mega -f test_sensors
```

---

## 🔌 Pin Map

### ATmega328P / Mega2560 (identical pins)

| Pin | Component | Notes |
|-----|-----------|-------|
| D4 | DHT22 | Temp/Humidity |
| D5 | SW-420 | Vibration |
| D6 | Buzzer (passive) | PWM tone |
| D7 | RGB LED — Red | |
| D8 | RGB LED — Green | |
| D9 | RGB LED — Blue | |
| D10 | SD Card CS | SPI |
| D2 | SIM800L RX (SoftSerial) | Via voltage divider |
| D3 | SIM800L TX (SoftSerial) | |
| A0 | Water level sensor | |
| A1 | MQ-2 Gas sensor | |
| A2 | Flame sensor | |
| A3 | Panic button | Pull-up |
| A4/SDA | DS3231 RTC | I2C |
| A5/SCL | DS3231 RTC | I2C |
| A6 | Battery voltage | 33kΩ/10kΩ divider |
| A7 | TP4056 CHRG pin | LOW = charging |

> **Mega-specific:** SIM800L uses `Serial1` (TX1=D18, RX1=D19) instead of SoftwareSerial. Controlled via `#ifdef TARGET_MEGA` in `config.h`.

### ESP32 Utility Node

| Pin | Component |
|-----|-----------|
| GPIO16 (RX2) | NEO-6M GPS TX |
| GPIO17 (TX2) | NEO-6M GPS RX |
| GPIO4 (RX1) | ATmega TX |
| GPIO5 (TX1) | ATmega RX |

---

## 📦 Bill of Materials (Key Components)

| Component | Purpose | Est. Cost (LKR) |
|-----------|---------|----------------|
| ATmega328P-PU | Main controller | 350 |
| Arduino Mega 2560 | Development/testing | 1,800 |
| ESP32 WROOM-32 | Utility node | 850 |
| SIM800L EVB | GSM/GPRS alerts | 900 |
| NEO-6M GPS | Location tracking | 750 |
| DHT22 | Temp + Humidity | 350 |
| MQ-2 | Gas/Smoke detection | 250 |
| DS3231 RTC module | Timestamps | 350 |
| Micro SD module | Data logging | 200 |
| SW-420 | Vibration/Earthquake | 80 |
| TP4056 module | Battery charging | 120 |
| 18650 LiPo cell | Battery backup | 400 |
| **Total (approx.)** | | **~13,700 LKR** |

---

## 🚀 Quick Start

### 1. Clone the repo
```bash
git clone https://github.com/Ravindu56/disaster-proof-blackbox-for-houses.git
cd disaster-proof-blackbox-for-houses
git checkout atmega   # or dev, esp32
```

### 2. Configure
Edit `include/config.h` — set your phone numbers, WiFi credentials, and MQTT broker:
```cpp
#define SMS_NUMBER_1   "+94XXXXXXXXX"
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASS      "YOUR_PASSWORD"
```

### 3. Build and flash
```bash
pio run -e mega -t upload        # Flash Mega for testing
pio device monitor -e mega       # Open serial monitor
```

### 4. Expected boot output
```
=============================================
  SentinelBox — booting
=============================================
[RTC]  OK
[SENS] OK
[SD]   OK — log.csv ready
[GSM]  init deferred
[BOOT] Complete

TEL,2026-03-22 01:00:21,30.4,75.7,53,142,953,0,0,0,11.98,FULL
EVT,2026-03-22 01:00:21,NORMAL,0.000000,0.000000
```
> GSM init is deliberately deferred 5 seconds after boot to prevent WDT reset loops.

---

## 📡 Serial Protocol (ATmega ↔ ESP32)

```
TEL,<timestamp>,<tempC>,<humidity>,<water>,<mq2>,<flame>,<vib>,<panic>,<flags>,<battV>,<battStatus>
EVT,<timestamp>,<hazardText>,<lat>,<lon>
GPS,<lat>,<lon>,<speed_kmh>,<sats>
PING / PONG
CMD,<command>
```

---

## 🔴 Hazard Bitmask

| Bit | Flag | Trigger |
|-----|------|---------|
| 0 | `FLOOD` | Water > threshold |
| 1 | `GAS` | MQ-2 > 450 ADC |
| 2 | `FIRE` | Flame < threshold OR Temp > 55°C |
| 3 | `QUAKE` | Vibration detected |
| 4 | `PANIC` | Button pressed |
| 5 | `HUMID` | Humidity > 95% |
| 6 | `TEMP` | Temperature > 55°C |

---

## 🔋 Power Management

- **Normal operation:** Mains 5V → Buck → 3.3V/5V buses
- **Mains failure detected:** `PIN_TP_CHRG` goes LOW → system logs power event, ESP32 notified
- **All-clear state:** ATmega enters WDT-gated sleep, wakes on vibration INT0 or 8s WDT overflow
- **WDT timeout:** 8 seconds — `Power::feedWatchdog()` called at every loop iteration and inside long blocking operations

---

## 🧪 Testing Workflow

Each module has a dedicated test environment. Develop and verify in isolation before integrating:

```bash
pio test -e mega -f test_rtc       # Verify DS3231 time
pio test -e mega -f test_sd        # Verify SD write/read
pio test -e mega -f test_sensors   # Calibrate all sensors
pio test -e mega -f test_gsm       # AT command walkthrough
```

---

## 📚 Course Alignment

This project is developed for **EC6020: Embedded Systems Design** at the University of Jaffna.

| EC6020 ILO | How SentinelBox addresses it |
|-----------|------------------------------|
| ILO 1 — Embedded system characteristics | Real-time constraints, reactive to environment |
| ILO 2 — Processor selection | ATmega328P chosen over μP; AVR family analysis |
| ILO 3 — Design metrics | NRE, unit cost (~13,700 LKR), power, time-to-market |
| ILO 4 — Hardware/Software co-design | Sensor drivers, ISR, WDT, sleep modes |
| ILO 5 — Design trade-offs | FPGA vs MCU, GSM on ATmega vs ESP32 analysis |

Reference: *Embedded Systems Design: A Unified Hardware/Software Introduction* — Vahid & Givargis

---

## 👨‍💻 Author

**P.A.P. Ravindu** | Department of Electrical & Electronic Engineering | University of Jaffna  
EC6020 Embedded Systems Design — 2025/2026

---

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

---

<p align="center">Built to survive what nature throws at us 🌪️</p>
