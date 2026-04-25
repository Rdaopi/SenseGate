# SenseGate — Zero-IT IIoT for Industrial Machines

> Independent, secure predictive maintenance without touching the customer's network.

SenseGate is an IIoT firmware system that enables Technowrapp to remotely monitor their industrial machines at customer sites — without requiring any access to the customer's LAN, Wi-Fi, or IT department. Data travels from the machine to the cloud via LoRa 868 MHz + NB-IoT binary SMS over SS7, completely independent of the customer's network infrastructure.

---

## How it works

```
[PLC + Sensors]
      |
      | Modbus RTU / I2C
      v
[Data Collector Node]  — STM32L073
  - Reads PLC registers every 35 min
  - Bit-packs data into 19 bytes
  - AES-128-CTR encrypts at source
  - Transmits via LoRa 868 MHz
      |
      | LoRa 868 MHz (non-IP)
      v
[Gateway Node]  — RAK4630 (nRF52840 + SX1262)
  - Receives encrypted packets
  - Buffers locally (200-day store-and-forward)
  - Role election via GPIO SIM_DETECT:
      SIM present  →  MASTER  →  NB-IoT SMS uplink
      No SIM       →  SLAVE   →  LoRa relay only
      |
      | NB-IoT / SS7 / Binary SMS 140B
      | (no customer internet required)
      v
[Twilio SMSC]  →  [AWS Lambda]  →  [TimescaleDB]  →  [Grafana]
```

Data is encrypted at the sensor node and decrypted only at AWS Lambda. No intermediate component — gateway, Twilio, or any other — ever sees the plaintext.

---

## Repository structure

```
SenseGate/
├── lib/                        # Shared firmware library
│   ├── aes_ctr.c / .h          # AES-128-CTR (pure C, no dependencies)
│   ├── payload_packer.c / .h   # 19-byte bit-packing algorithm
│   ├── rolling_buffer.c / .h   # Rolling redundancy (current + previous packet)
│   └── store_forward.c / .h    # 200-slot store-and-forward ring buffer
│
├── node_collector/             # Data collector firmware (STM32L073)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── include/
│   │   ├── hal.h               # Hardware abstraction layer interface
│   │   └── modbus_sim.h        # Modbus register map
│   └── src/
│       ├── main.c              # Sensor loop: read → pack → encrypt → TX
│       ├── hal_sim.c           # Simulated HAL (replace with hal_hw.c for hardware)
│       └── modbus_sim.c        # Simulated Modbus slave (9 PLC registers)
│
├── node_gateway/               # Gateway firmware (RAK4630 / nRF52840)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── include/
│   │   ├── hal.h               # Hardware abstraction layer interface
│   │   └── role_election.h     # Role election interface
│   └── src/
│       ├── main.c              # Gateway loop: RX → buffer → TX via NB-IoT
│       ├── hal_sim.c           # Simulated HAL (replace with hal_hw.c for hardware)
│       └── role_election.c     # Boot-time role election via SIM_DETECT GPIO
│
└── README.md
```

---

## Firmware architecture

### Payload format (19 bytes)

| Bytes | Field | Resolution |
|-------|-------|------------|
| 0–1 | Temperature (12 bit) | 0.1 °C, range −40 to +85 °C |
| 1–2 | Humidity (8 bit) | 0.5 %, range 0–100 % |
| 2–3 | Vibration (10 bit) | 0.016 g, range 0–16 g |
| 3–5 | Pressure (14 bit) | 0.06 hPa, range 300–1100 hPa |
| 6–8 | PLC cycles (24 bit) | raw counter |
| 9–10 | PLC hours (16 bit) | hours of operation |
| 11 | PLC status (8 bit) | bit 0 = running, bit 1 = alarm |
| 12–13 | Sequence number (16 bit) | packet counter |
| 14 | Device ID (8 bit) | node identifier |
| 15–16 | Reserved | — |
| 17–18 | CRC16-CCITT | integrity check |

### Transmission packet (38 bytes)

Every LoRa transmission carries `current packet (19B) + previous packet (19B)`. If a packet is lost in transmission, the next one automatically recovers it — no retransmission protocol needed.

### AES-128-CTR encryption

- Stream cipher: 19 bytes in → 19 bytes out, no padding
- Key stored in ATECC608B secure element (hardware tamper-proof)
- Nonce derived from device ID + sequence number
- Encryption happens at the sensor node — the gateway never decrypts
- Pure C implementation, zero external dependencies

### Store-and-forward

- Ring buffer: 200 slots × 38 bytes
- Data is preserved during cellular outages
- Buffer drains automatically in FIFO order when connection is restored
- On real hardware: backed by W25Q32 NOR flash (1 MB = 26,000+ packets)

### Role election

At boot, the gateway reads the `SIM_DETECT` GPIO pin:

| SIM_DETECT | Role | Behaviour |
|---|---|---|
| HIGH (SIM present) | MASTER | Activates NB-IoT modem, sends binary SMS to cloud |
| LOW (no SIM) | SLAVE | LoRa relay only, forwards packets to master |

To change the master: remove the SIM from the broken gateway and insert it into any other node. The new master elects itself in seconds with no reconfiguration.

---

## Hardware components

### Data collector node (STM32L073)

| Component | Role | Interface |
|---|---|---|
| STM32L073 | Main MCU — ARM Cortex-M0+ @ 32 MHz | — |
| SX1261 | LoRa TX — 868 MHz, SF7–SF12 | SPI |
| ATECC608B | Secure element — AES-128 key storage | I2C |
| MAX3485 | RS-485 transceiver for Modbus RTU | UART |
| SHT45 | Temperature + humidity | I2C |
| ADXL345 | Vibration / accelerometer | I2C |
| BMP390 | Barometric pressure | I2C |
| W25Q32 | NOR flash — store-and-forward buffer | SPI |
| LiPo 2000 mAh | Battery — 3–4 year life | — |

### Gateway node (RAK4630)

| Component | Role | Interface |
|---|---|---|
| nRF52840 | Main MCU — ARM Cortex-M4 @ 64 MHz | — |
| SX1262 | LoRa RX/TX — 868 MHz (integrated in RAK4630) | SPI |
| Quectel BC660K-GL | NB-IoT / LTE-M modem (master only) | UART |
| ATECC608B | Secure element | I2C |
| W25Q32 | NOR flash — 200-day store-and-forward | SPI |
| MAX3485 | RS-485 for local PLC (optional) | UART |

---

## Building and running

### Prerequisites

- [nRF Connect SDK v2.9.0](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/installation.html)
- [nRF Connect for VS Code](https://marketplace.visualstudio.com/items?itemName=nordic-semiconductor.nrf-connect-extension-pack)
- [QEMU](https://www.qemu.org/download/) (for simulation)

### Build — data collector

```bash
cd node_collector
west build -b qemu_cortex_m3 --pristine
```

### Run — data collector (QEMU)

```bash
# Windows
export PATH="/c/Program Files/qemu:$PATH"

"/c/Program Files/qemu/qemu-system-arm.exe" \
  -cpu cortex-m3 \
  -machine lm3s6965evb \
  -nographic \
  -kernel build/node_collector/zephyr/zephyr.elf
```

Press `CTRL+A` then `X` to exit QEMU.

### Build — gateway

```bash
cd node_gateway
west build -b qemu_cortex_m3 --pristine
```

### Run — gateway (QEMU)

```bash
"/c/Program Files/qemu/qemu-system-arm.exe" \
  -cpu cortex-m3 \
  -machine lm3s6965evb \
  -nographic \
  -kernel build/node_gateway/zephyr/zephyr.elf
```

---

## Hardware integration

The firmware uses a Hardware Abstraction Layer (HAL) that cleanly separates simulated and real hardware. To integrate physical components:

1. Create `src/hal_hw.c` in each project with the same function signatures as `hal_sim.c`
2. Replace `hal_sim.c` with `hal_hw.c` in `CMakeLists.txt`
3. Change the build target from `qemu_cortex_m3` to `rak4631_nrf52840`

The `main.c` and all library files remain unchanged.

### HAL functions to implement in `hal_hw.c`

| Function | Simulation | Hardware |
|---|---|---|
| `hal_sensor_read()` | Modbus registers from RAM | UART Modbus RTU via MAX3485 |
| `hal_flash_write/read()` | Ring buffer in RAM | SPI on W25Q32 |
| `hal_radio_tx()` | printk | Zephyr LoRa driver on SX1261 |
| `hal_radio_rx()` | Fixed test packet | Zephyr LoRa driver on SX1262 |
| `hal_modem_send_sms()` | printk | AT commands on Quectel BC660K |
| `hal_get_role()` | Variable in RAM | `gpio_pin_get(SIM_DETECT)` |
| `hal_crypto_get_key()` | Hardcoded key | `atcab_read_zone()` on ATECC608B |

### Hardware validation sequence (in order)

1. **NB-IoT SMS test** — connect Quectel BC660K via USB, send AT commands manually via PuTTY, verify Twilio receives the SMS. This is the most critical test — if it fails, everything else is blocked.
2. **LoRa link test** — two RAK4630 nodes, one transmits a 38-byte packet, the other receives and prints. Verify range inside a metal industrial building.
3. **Modbus RTU test** — connect MAX3485 to the Technowrapp PLC, verify register addresses with ModScan before hardcoding them in `modbus_sim.h`.

---

## Key design decisions

**Why LoRa 868 MHz?**
Non-IP radio — physically impossible to route from SenseGate's radio into the customer's PLC network. 868 MHz penetrates metal and concrete structures better than 2.4 GHz (Wi-Fi, Bluetooth). EU868 band with 0.014% duty cycle — 70× below the ETSI 1% legal limit.

**Why NB-IoT + SS7?**
SMS travels via the SS7 signaling plane — the same infrastructure that carries voice calls. It is completely independent of the internet. The customer's IT department has no visibility and no control over it. NB-IoT provides 20 dB better indoor penetration than standard 4G LTE.

**Why AES-128-CTR and not AES-128-CBC?**
CTR is a stream cipher: 19 bytes in → 19 bytes out, no padding. This is essential because the payload must fit exactly into the binary SMS budget. CBC would add up to 16 bytes of padding, breaking the 140-byte SMS math.

**Why a pure C AES implementation?**
The nRF Connect SDK (v2.9.0) PSA Crypto layer is not easily available on QEMU targets without NRF_SECURITY. The pure C implementation is functionally identical, has zero dependencies, compiles on any Zephyr target, and will be replaced by ATECC608B hardware acceleration on the real chip.

**Why same PCB for master and gateway?**
Single SKU simplifies logistics and dramatically improves resilience. If the master gateway fails, a technician moves the SIM card to any other node — no special hardware, no reconfiguration, no IT involvement. The new master boots in seconds.

---

## Modbus register map

Register addresses to be confirmed with Technowrapp before hardware integration.

| Register | Address | Encoding | Range |
|---|---|---|---|
| Temperature | 40001 | raw × 10 | −400 to +850 |
| Humidity | 40002 | raw × 2 | 0 to 200 |
| Vibration | 40003 | raw × 64 | 0 to 1023 |
| Pressure HIGH | 40004 | (raw − 300) × 16 HIGH word | — |
| Pressure LOW | 40005 | (raw − 300) × 16 LOW word | — |
| PLC cycles HIGH | 40006 | 32-bit counter HIGH word | — |
| PLC cycles LOW | 40007 | 32-bit counter LOW word | — |
| PLC hours | 40008 | hours of operation | 0 to 65535 |
| PLC status | 40009 | bit 0 = running, bit 1 = alarm | — |

---

## Project status

| Component | Status |
|---|---|
| Bit-packing algorithm | ✅ Complete — validated on QEMU |
| AES-128-CTR encryption | ✅ Complete — validated on QEMU |
| Rolling redundancy | ✅ Complete — validated on QEMU |
| Store-and-forward buffer | ✅ Complete — validated on QEMU |
| HAL abstraction layer | ✅ Complete |
| Role election | ✅ Complete — validated on QEMU |
| Modbus RTU simulation | ✅ Complete — validated on QEMU |
| LoRa driver (hardware) | ⏳ Pending hardware |
| NB-IoT AT commands | ⏳ Pending hardware |
| ATECC608B key read | ⏳ Pending hardware |
| W25Q32 SPI flash | ⏳ Pending hardware |
| Modbus RTU (real PLC) | ⏳ Pending PLC register map from Technowrapp |
| Cloud pipeline | ⏳ In progress |

---

## License

SenseGate firmware and algorithms are proprietary IP developed by the SenseGate team.

Third-party components used:
- **Zephyr RTOS** — Apache 2.0
- **nRF Connect SDK** — Nordic Semiconductor (various open source licenses)
- **QEMU** — GPL v2
