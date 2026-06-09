"""
SenseGate - Demo bridge: NUCLEO collector -> RAK4631 gateway (BLE) -> cloud

Full pipeline:

    PC (modbus_sim)
      -> NUCLEO (AES, pack, LoRa TX via Ebyte)
        -> RAK4631 (LoRa RX, store-forward, NB-IoT sim over BLE)
          -> hw_bridge_demo.py receives BLE notifications
            -> POST /sms -> Docker (Twilio sim -> AWS IoT -> Grafana)

Usage:
    python hw_bridge_demo.py --collector COM3
    python hw_bridge_demo.py --collector COM3 --url http://localhost:8080

Prerequisites:
    - Docker stack running:   cd cloud && docker compose up -d
    - NUCLEO flashed and connected via USB on --collector port
    - RAK4631 flashed with BLE firmware, powered on and advertising "SenseGate-GW"
    - pip install bleak pyserial
"""

import argparse
import asyncio
import json
import re
import sys
import threading
import time
import urllib.parse
import urllib.request
import urllib.error

import serial
from bleak import BleakClient, BleakScanner

# Nordic UART Service UUIDs (standard)
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # RAK notifies on this
NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # PC writes on this

GATEWAY_NAME = "SenseGate-GW"
DEBUG = False  # set via --debug flag

RE_COLLECTOR_TX = re.compile(
    r"\[HAL (?:COLLECTOR SIM|HW)\] LoRa TX \d+ bytes: ([0-9A-Fa-f]+)"
)
RE_GATEWAY_NBIOT = re.compile(
    r"SMS:([0-9A-Fa-f]{50,})"
)

FULL_HEX_LEN = 100  # 50 bytes * 2 hex chars


def post_sms(base_url, hex_str):
    payload = urllib.parse.urlencode({"Body": hex_str.upper(), "From": "+hw"}).encode()
    req = urllib.request.Request(
        f"{base_url}/sms",
        data=payload,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        return {"error": f"HTTP {e.code}", "body": e.read().decode()}
    except Exception as e:
        return {"error": str(e)}


def handle_gateway_line(line, base_url, counter):
    m = RE_GATEWAY_NBIOT.search(line)
    if not m:
        return
    hex_str = m.group(1).upper()
    if DEBUG:
        print(f"[bridge] SMS hex len={len(hex_str)} chars ({len(hex_str)//2} bytes)")
        sys.stdout.flush()
    if len(hex_str) < 100:
        if DEBUG:
            print(f"[bridge] WARNING: incomplete packet ({len(hex_str)} chars), skipping")
            sys.stdout.flush()
        return
    hex_str = hex_str[:100]  # take exactly 50 bytes
    resp = post_sms(base_url, hex_str)
    if resp.get("error"):
        time.sleep(1)
        resp = post_sms(base_url, hex_str)
    status  = resp.get("status", resp.get("error", "?"))
    results = resp.get("results", [])
    stored  = any(r.get("stored") for r in results)
    db_tag  = "stored" if stored else "dup"
    counter[0] += 1
    print(f"[bridge] --> NB-IoT POST #{counter[0]}  cloud={status}  {db_tag}")
    sys.stdout.flush()


async def ble_gateway_task(base_url, stop_event, counter):
    """Scan for SenseGate-GW, connect, receive NUS notifications."""
    while not stop_event.is_set():
        print(f"[BLE] Scanning for '{GATEWAY_NAME}'...")
        try:
            device = await BleakScanner.find_device_by_name(GATEWAY_NAME, timeout=10.0)
        except Exception as e:
            print(f"[BLE] Scan error: {e} — retrying in 5s")
            await asyncio.sleep(5)
            continue

        if device is None:
            print(f"[BLE] '{GATEWAY_NAME}' not found — retrying in 5s")
            await asyncio.sleep(5)
            continue

        print(f"[BLE] Found {device.name} ({device.address}) — connecting...")
        buf = ""

        def on_notify(sender, data):
            nonlocal buf
            buf += data.decode("utf-8", errors="replace")
            if DEBUG:
                print(f"[BLE/raw] {data.hex()}")
                sys.stdout.flush()
            # Extract complete SMS packets (exactly 100 hex chars) from raw buffer
            while True:
                m = re.search(r"SMS:([0-9A-Fa-f]{100})", buf)
                if not m:
                    break
                hex_str = m.group(1).upper()
                if DEBUG:
                    print(f"[GATEWAY/BLE] SMS received (100 chars)")
                    sys.stdout.flush()
                handle_gateway_line(f"SMS:{hex_str}", base_url, counter)
                buf = buf[m.end():]
            # Keep buffer bounded
            if len(buf) > 512:
                buf = buf[-256:]

        try:
            async with BleakClient(device) as client:
                print(f"[BLE] Connected to {device.name}")
                if DEBUG:
                    services = client.services
                    print("[BLE] Services found:")
                    for svc in services:
                        print(f"  SVC {svc.uuid}")
                        for ch in svc.characteristics:
                            print(f"    CHR {ch.uuid}  props={ch.properties}")
                await client.start_notify(NUS_TX_CHAR_UUID, on_notify)
                print("[BLE] Subscribed to NUS TX — waiting for data...")
                while not stop_event.is_set() and client.is_connected:
                    await asyncio.sleep(0.5)
                await client.stop_notify(NUS_TX_CHAR_UUID)
        except Exception as e:
            print(f"[BLE] Connection lost: {e} — reconnecting in 3s")
            await asyncio.sleep(3)


def collector_thread(port, baud, stop_event):
    """Read NUCLEO serial and print LoRa TX lines for monitoring."""
    while not stop_event.is_set():
        try:
            ser = serial.Serial(port, baud, timeout=1)
            print(f"[COLLECTOR] Opened {port}")
            while not stop_event.is_set():
                try:
                    raw = ser.readline()
                except serial.SerialException as e:
                    print(f"[COLLECTOR] Serial error: {e}")
                    break
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if line:
                    print(f"[COLLECTOR] {line}")
                    sys.stdout.flush()
            ser.close()
        except serial.SerialException as e:
            print(f"[COLLECTOR] Cannot open {port}: {e} — retrying in 3s")
            time.sleep(3)


def run(collector_port, baud, base_url):
    stop_event = threading.Event()
    counter = [0]

    print("=" * 60)
    print("  SenseGate - Full Demo Bridge (BLE)")
    print("=" * 60)
    print(f"  Collector : NUCLEO on {collector_port}")
    print(f"  Gateway   : RAK4631 via BLE ({GATEWAY_NAME})")
    print(f"  Cloud     : {base_url}/sms")
    print(f"  Pipeline  : NUCLEO --LoRa--> RAK4631 --BLE--> PC --HTTP--> cloud")
    print("  Ctrl+C to stop")
    print("=" * 60)
    print()

    # Start NUCLEO serial reader thread
    t = threading.Thread(
        target=collector_thread,
        args=(collector_port, baud, stop_event),
        daemon=True,
    )
    t.start()

    # Run BLE event loop in main thread
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    async def main_loop():
        ble_task = asyncio.create_task(
            ble_gateway_task(base_url, stop_event, counter)
        )
        try:
            await ble_task
        except asyncio.CancelledError:
            pass

    try:
        loop.run_until_complete(main_loop())
    except KeyboardInterrupt:
        print("\n[bridge] Stopped by user.")
    finally:
        stop_event.set()
        loop.close()

    t.join(timeout=3)
    print(f"\n[bridge] Done. Total packets forwarded to cloud: {counter[0]}")


def main():
    parser = argparse.ArgumentParser(description="SenseGate full demo bridge (BLE gateway)")
    parser.add_argument("--collector", default="COM3",                  help="NUCLEO serial port (default: COM3)")
    parser.add_argument("--baud",      type=int, default=115200,        help="Baud rate (default: 115200)")
    parser.add_argument("--url",       default="http://localhost:8080", help="Cloud base URL")
    parser.add_argument("--debug",     action="store_true",             help="Enable verbose debug output")
    args = parser.parse_args()

    global DEBUG
    DEBUG = args.debug
    run(args.collector, args.baud, args.url)


if __name__ == "__main__":
    main()
