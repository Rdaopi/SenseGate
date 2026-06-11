# SenseGate — Zero-IT IIoT for Industrial Machines

> Independent, secure predictive maintenance without touching the customer's network.

SenseGate enables remote monitoring of industrial machines at customer sites without requiring access to the customer's LAN, Wi-Fi, or IT department. Data travels from the machine to the cloud via LoRa 868 MHz + NB-IoT binary SMS, completely independent of the customer's network.

---

## System architecture

```
[PLC / Sensors]
      | Modbus RTU
      v
[Collector Node]  — NUCLEO-L073RZ + Ebyte E22-900T22S (SX1262)
  - Reads PLC registers every 35 min
  - Packs data into 25 bytes
  - AES-128-CTR encrypts at source
  - Transmits via LoRa 868 MHz
      |
      | LoRa 868 MHz
      v
[Gateway Node]  — RAK4631 (nRF52840 + SX1262)
  - Receives encrypted LoRa packets
  - Buffers in RAM (200-slot store-and-forward)
  - Role election via SIM_DETECT GPIO:
      HIGH (SIM present) → MASTER → NB-IoT SMS uplink
      LOW  (no SIM)      → SLAVE  → LoRa relay only
      |
      | NB-IoT SMS (demo: BLE → PC → HTTP)
      v
[Docker stack]  →  [Twilio sim]  →  [AWS IoT sim]  →  [TimescaleDB]  →  [Grafana]
```

---

## Repository structure

```
SenseGate/
├── node_collector/             # Collector firmware — NUCLEO-L073RZ (Zephyr RTOS)
│   ├── src/
│   │   ├── main.c
│   │   ├── hal_sim.c           # Simulated HAL (no hardware needed)
│   │   ├── hal_lora.c          # Real hardware HAL (Ebyte via Zephyr LoRa driver)
│   │   └── hal_hw.c            # Modbus RTU hardware HAL
│   └── include/
│
├── node_gateway_arduino/       # Gateway firmware — RAK4631 (Arduino)
│   ├── node_gateway_arduino.ino
│   ├── hal_select.h            # Switch between HAL_USE_SIM / HAL_USE_LORA
│   ├── hal_sim.cpp             # Simulated HAL (no hardware needed)
│   ├── hal_lora.cpp            # Real hardware HAL (SX1262 via SX126x-Arduino)
│   ├── hal_modem.cpp           # NB-IoT HAL (demo: BLE output)
│   ├── role_election.cpp       # SIM_DETECT GPIO role election
│   └── store_forward.cpp       # RAM ring buffer (200 slots × 50 bytes)
│
├── cloud/                      # Docker stack
│   ├── docker-compose.yml
│   └── lambda/                 # Twilio sim + AWS IoT sim + TimescaleDB writer
│
├── hw_bridge.py                # Bridge: NUCLEO → gateway QEMU → cloud (legacy)
├── hw_bridge_demo.py           # Bridge: NUCLEO → RAK4631 via BLE → cloud (demo)
├── requirements.txt            # Python dependencies
└── README.md
```

---

## Quick start — demo (no LoRa hardware needed)

This mode runs the full pipeline with simulated LoRa. The RAK4631 generates packets internally and forwards them to the cloud via BLE.

### 1. Prerequisites

- **Arduino IDE 2.x**
- **Python 3.10+**
- **Docker Desktop**
- **Bluetooth adapter** on your PC

### 2. Install RAKwireless board package

In Arduino IDE: **File → Preferences → Additional boards manager URLs**, add:
```
https://raw.githubusercontent.com/RAKwireless/RAKwireless-Arduino-BSP-Index/main/package_rakwireless_index.json
```
Then: **Tools → Board → Boards Manager** → search `RAKwireless nRF` → install v1.3.3.

### 3. Patch the Arduino core

The RAKwireless core requires a few fixes to compile correctly. Run once after installing:

```bash
python setup_arduino_core.py
```

Then restart Arduino IDE.

### 4. Install Python dependencies

```bash
pip install -r requirements.txt
```

### 5. Flash the gateway firmware

1. Open `node_gateway_arduino/node_gateway_arduino.ino` in Arduino IDE
2. Select: **Tools → Board → WisBlock RAK4631**
3. Select: **Tools → SoftDevice → S140 6.1.1**
4. Verify `hal_select.h` has `#define HAL_USE_SIM`
5. Double-press reset on RAK4631 to enter bootloader (red LED blinks)
6. Upload (Ctrl+U) — wait for `Device programmed`

### 6. Start the Docker stack

```bash
cd cloud
docker compose up -d
```

Grafana is available at [http://localhost:3000](http://localhost:3000) (admin/admin).

### 7. Start the bridge

```bash
python hw_bridge_demo.py
```

The bridge scans for `SenseGate-GW` via BLE, connects automatically, and starts forwarding packets to the cloud. Add `--debug` for verbose output.

---

## Full demo — with real LoRa hardware

Requires NUCLEO-L073RZ + Ebyte E22-900T22S soldered and flashed.

### 1. Flash the collector (NUCLEO)

```bash
cd node_collector
# Set HAL_USE_LORA in CMakeLists.txt
west build -b nucleo_l073rz --pristine
west flash
```

### 2. Flash the gateway (RAK4631)

1. In `hal_select.h` change to `#define HAL_USE_LORA`
2. Upload via Arduino IDE as above

### 3. Start the bridge

```bash
python hw_bridge_demo.py --collector COM3
```

Replace `COM3` with the actual NUCLEO serial port.

---

## LoRa parameters (must match on both nodes)

| Parameter | Value |
|---|---|
| Frequency | 868 MHz |
| Bandwidth | 125 kHz |
| Spreading Factor | SF10 |
| Coding Rate | 4/5 |
| Preamble | 8 |
| TX Power | 14 dBm |

---

## Role election

At boot, the gateway reads `WB_IO1` (pin 17, WisBlock slot):

| SIM_DETECT | Role | Behaviour |
|---|---|---|
| HIGH | MASTER | Sends packets via NB-IoT (demo: BLE → cloud) |
| LOW | SLAVE | LoRa relay only |

To force MASTER in demo mode: `HAL_USE_SIM` always elects MASTER regardless of GPIO.

---

## Bridge options

```
python hw_bridge_demo.py [--collector PORT] [--baud BAUD] [--url URL] [--debug]

  --collector  NUCLEO serial port (default: COM3)
  --baud       Baud rate (default: 115200)
  --url        Cloud base URL (default: http://localhost:8080)
  --debug      Verbose BLE and packet debug output
```

---

## Hardware

### Collector node
| Component | Role |
|---|---|
| NUCLEO-L073RZ | STM32L073 dev board |
| Ebyte E22-900T22S | SX1262 LoRa module (868 MHz) |

### Gateway node
| Component | Role |
|---|---|
| RAK4631 | nRF52840 + SX1262, BLE 5.0 |
| RAK19007 | WisBlock base board |

---

## Project status

| Component | Status |
|---|---|
| Collector firmware (sim) | ✅ Working on NUCLEO |
| Gateway firmware (sim) | ✅ Working on RAK4631 |
| BLE uplink (gateway → PC) | ✅ Working |
| Cloud pipeline (Docker) | ✅ Working |
| Grafana dashboard | ✅ Working |
| LoRa real hardware | ⏳ Pending Ebyte soldering |
| NB-IoT real hardware | ⏳ Pending modem |
| Modbus RTU (real PLC) | ⏳ Pending PLC register map |

---

## License

Proprietary — SenseGate team. All rights reserved.
