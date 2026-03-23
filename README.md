# SentinelBox 🛡️

**Disaster-Proof Household Monitoring System**  
EC6020: Embedded Systems Design — Group 15 | University of Peradeniya

[![PlatformIO](https://img.shields.io/badge/PlatformIO-5.x-orange)](https://platformio.org)
[![ATmega328P](https://img.shields.io/badge/Core-ATmega328P-blue)](https://www.microchip.com)
[![ESP32](https://img.shields.io/badge/Utility-ESP32%20WROOM--32-green)](https://www.espressif.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)

---

## Overview

SentinelBox is an embedded blackbox system that continuously monitors a
household for disasters — fire, gas leaks, flooding, structural vibration —
and alerts occupants via SMS even when mains electricity fails. It uses a
dual-node architecture with an ATmega328P as the low-power core sensor node
and an ESP32 WROOM-32 as the communication utility node.

```
┌─────────────────────┐    UART 9600     ┌─────────────────────┐
│   ATmega328P        │ ───────────────► │   ESP32 WROOM-32    │
│   Core Node         │ ◄─────────────── │   Utility Node      │
│                     │    ACK/CMD        │                     │
│  -  DHT22 Temp/Hum   │                  │  -  SIM800L GSM/SMS  │
│  -  MQ-2 Gas         │    D2 Wake ────► │  -  NEO-6M GPS       │
│  -  Flame sensor     │                  │  -  WiFi / MQTT      │
│  -  Water sensor     │                  │  -  Deep sleep       │
│  -  Vibration        │                  │    10µA             │
│  -  DS3231 RTC       │                  │                     │
│  -  SD card log      │                  │                     │
│  -  Power-Down sleep │                  │                     │
│    0.1µA            │                  │                     │
└─────────────────────┘                  └─────────────────────┘
         │                                        │
    Bus A 5V                                 Bus B 4.2V
         └────────────── LiPo 3000mAh ───────────┘
                         TP4056 + DW01
```

---

## Features

- 🔥 **Multi-hazard detection** — temperature, humidity, gas, flame, water, vibration
- 📱 **SMS alerts** with GPS location via SIM800L + NEO-6M
- 🔋 **Battery backup** — survives mains power outages
- 😴 **Smart sleep protocol** — ~31 days battery life in idle mode
- 💾 **SD card black-box logging** with RTC timestamps
- ⚡ **Power-aware design** — 328P Power-Down (0.1µA) + ESP32 deep sleep (10µA)
- 📡 **WiFi/MQTT** ready for cloud dashboard integration

---

## Hardware

### Core Node — ATmega328P

| Component | Model | Connection |
|-----------|-------|------------|
| MCU | ATmega328P (bare chip) | — |
| Temp/Humidity | DHT22 | D8 |
| Gas | MQ-2 | A2 (analog), D12 (digital) |
| Flame | IR Flame Module | D6 |
| Water | Resistive Sensor | D7 |
| Vibration | SW-420 | D3 |
| RTC | DS3231 | SDA/SCL (I2C) |
| Storage | MicroSD | D10–D13 (SPI) |
| Power sense | TP4056 CHRG | A0 |
| Battery voltage | Voltage divider | A1 |
| ESP32 wake | D2 → GPIO33 | Digital OUT |

### Utility Node — ESP32 WROOM-32

| Component | Model | UART | Pins |
|-----------|-------|------|------|
| GSM/SMS | SIM800L EVB | UART1 @ 57600 | GPIO12 TX, GPIO14 RX |
| GPS | NEO-6M | UART2 | GPIO16 RX, GPIO17 TX |
| Core link | ATmega328P | UART0 @ 9600 | GPIO1 TX, GPIO3 RX |
| Wake input | — | — | GPIO33 (ext0) |

---

## Power Architecture

```
Mains 230V AC
    │
[5V/2A Adapter]
    ├──[Buck A: 5V/500mA]──► Bus A: ATmega328P + All Sensors
    ├──[Buck B: 4.2V/3A] ──► Bus B: ESP32 + SIM800L + GPS
    └──[TP4056 Charger]  ──► LiPo 3000mAh
                                 │
                            [DW01 Protection]
                                 │
                            [Diode OR] ──► Bus A + Bus B (battery backup)
```

### Battery Life Estimates

| Mode | 328P | ESP32 | Current | Duration |
|------|------|-------|---------|----------|
| Mains normal | Active | Active | ~200mA | — |
| Battery idle | Power-Down (30s cycle) | Deep sleep | ~4mA | ~31 days |
| Critical battery | Power-Down (5min cycle) | Deep sleep | ~2mA | ~62 days |

---

## Power Modes

| Mode | Trigger | 328P | ESP32 |
|------|---------|------|-------|
| **Mains Normal** | A0 LOW | Full operation | Full operation |
| **Battery Idle** | A0 HIGH | 30s sleep cycle | Deep sleep |
| **Emergency** | Hazard flags | Wake briefly | Wake → SMS → sleep |
| **Mains Restored** | A0 LOW again | Full operation | Wake → notify → active |

---

## Inter-Node Protocol

UART: 9600 baud, 8N1, `\n` terminated frames

| Frame | Sender | Format | Purpose |
|-------|--------|--------|---------|
| `TEL` | 328P | `TEL,<ms>,T:<v>,H:<v>,W:<v>,G:<v>,F:<v>,V:<v>,B:<v>,FLAGS:<hex>` | Telemetry |
| `EVT` | 328P | `EVT,<flags>,<ts>` | Hazard event |
| `PWR` | 328P | `PWR:BATT` / `PWR:MAINS` | Power state change |
| `ACK` | ESP32 | `ACK,<ms>` | Acknowledge |
| `SLEEP` | ESP32 | `SLEEP` | About to deep sleep |
| `WAKE` | ESP32 | `WAKE` | Awake and ready |

---

## Repository Structure

```
SentinelBox/
├── src/
│   ├── atmega/          # Core node firmware (328P)
│   │   ├── main.cpp
│   │   ├── sensors.cpp/h
│   │   ├── hazards.cpp/h
│   │   ├── storage.cpp/h
│   │   ├── rtctime.cpp/h
│   │   ├── comms.cpp/h
│   │   ├── leds.cpp/h
│   │   └── power.cpp/h
│   └── esp32/           # Utility node firmware (ESP32)
│       ├── main.cpp
│       ├── gsm.cpp/h
│       ├── gps.cpp/h
│       ├── comms.cpp/h
│       └── power.cpp/h
├── test/
│   ├── test_gsm/        # SIM800L + ESP32 link test
│   ├── test_rtc/        # DS3231 RTC test
│   ├── test_sd/         # SD card test
│   └── test_sensors/    # Sensor suite test
├── include/
│   └── config.h         # Pin defines, thresholds
├── docs/
│   └── spec.tex         # LaTeX specification document
├── platformio.ini
└── README.md
```

---

## Getting Started

### Prerequisites

```bash
# Install PlatformIO CLI
pip install platformio

# Clone the repository
git clone https://github.com/Ravindu56/SentinelBox.git
cd SentinelBox

# Create virtual environment
python -m venv .venv && source .venv/bin/activate
pip install platformio
```

### Build & Upload

```bash
# Core node (ATmega328P bare chip)
pio run -e atmega328p -t upload

# Core node (Mega 2560 for development)
pio run -e mega -t upload

# Utility node (ESP32)
pio run -e esp32 -t upload
```

### Monitor

```bash
# Core node serial monitor
pio device monitor -e mega

# Utility node serial monitor
pio device monitor -e esp32
```

### Run Tests

```bash
# GSM + Mega link test
pio run -e test_gsm -t upload && pio device monitor -e test_gsm

# Sensor test
pio run -e test_sensors -t upload && pio device monitor -e test_sensors

# RTC test
pio run -e test_rtc -t upload && pio device monitor -e test_rtc

# SD card test
pio run -e test_sd -t upload && pio device monitor -e test_sd
```

---

## Wiring Quick Reference

### Level Shifter (328P TX → ESP32 RX)
```
328P D1 (5V) ──[10kΩ]──┬── ESP32 GPIO16 RX
                        └──[20kΩ]── GND   (junction = 3.3V)

ESP32 GPIO17 (3.3V) ───── 328P D0 RX   (direct, no divider)
```

### Wake Pin (328P D2 → ESP32 GPIO33)
```
328P D2 ──[10kΩ series]──┬── ESP32 GPIO33
                         └──[10kΩ pull-down]── GND
```

### SIM800L Level Shifter
```
ESP32 GPIO12 (3.3V) ──[10kΩ]──┬── SIM800L RXD
                               └──[20kΩ]── GND

SIM800L TXD ─────────────────── ESP32 GPIO14  (direct)
```

---

## Branches

| Branch | Purpose |
|--------|---------|
| `main` | Stable releases |
| `atmega` | Active development — core node firmware |
| `dev` | Experimental features |

---

## Course Information

- **Module:** EC6020 — Embedded Systems Design
- **Credits:** 3
- **Department:** Electrical & Electronic Engineering
- **University:** University of Peradeniya, Sri Lanka
- **Reference:** *Embedded Systems Design: A Unified Hardware/Software Introduction* — Vahid & Givargis

---

## License

MIT License — see [LICENSE](LICENSE) for details.
