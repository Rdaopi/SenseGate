"""
SenseGate - Demo bridge: NUCLEO collector -> RAK4631 gateway (real HW) -> cloud

Full pipeline:

    PC (modbus_sim)
      -> NUCLEO (AES, pack, LoRa TX via Ebyte)
        -> RAK4631 (LoRa RX, store-forward, NB-IoT sim on USB-C)
          -> hw_bridge_demo.py reads RAK USB-C serial
            -> POST /sms -> Docker (Twilio sim -> AWS IoT -> Grafana)

Usage:
    python hw_bridge_demo.py --collector COM3 --gateway COM5
    python hw_bridge_demo.py --collector COM3 --gateway COM5 --url http://localhost:8080

Prerequisites:
    - Docker stack running:   cd cloud && docker compose up -d
    - NUCLEO flashed with HAL_USE_LORA (Ebyte connected)
    - RAK4631 flashed with HAL_USE_LORA + Serial output enabled
    - pyserial installed:     pip install pyserial

Fallback (no LoRa HW):
    - NUCLEO with HAL_USE_SIM, RAK4631 with HAL_USE_SIM
    - bridge reads NUCLEO serial for LoRa TX lines and RAK serial for NB-IoT TX lines
"""

import argparse
import json
import sys
import threading
import time
import urllib.parse
import urllib.request
import urllib.error
import re

import serial

# --- regex patterns ---
# NUCLEO collector outputs this when it transmits a LoRa packet
RE_COLLECTOR_TX = re.compile(
    r"\[HAL (?:COLLECTOR SIM|HW)\] LoRa TX \d+ bytes: ([0-9A-Fa-f]+)"
)

# RAK4631 gateway outputs this when it forwards via NB-IoT (demo mode)
RE_GATEWAY_NBIOT = re.compile(
    r"\[GATEWAY\] NB-IoT TX simulation: ([0-9A-Fa-f]+)"
)

FULL_HEX_LEN = 100   # 50 bytes * 2 hex chars


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


def read_serial(port_name, baud, label, line_cb, stop_event):
    """Thread: reads lines from a serial port and calls line_cb(line) for each."""
    while not stop_event.is_set():
        try:
            ser = serial.Serial(port_name, baud, timeout=1)
            print(f"[bridge] {label} opened on {port_name}")
            while not stop_event.is_set():
                try:
                    raw = ser.readline()
                except serial.SerialException as e:
                    print(f"[bridge] {label} serial error: {e}")
                    break
                if not raw:
                    continue
                try:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                except Exception:
                    continue
                if line:
                    line_cb(line)
            ser.close()
        except serial.SerialException as e:
            print(f"[bridge] {label} cannot open {port_name}: {e} — retrying in 3s")
            time.sleep(3)


def run(collector_port, gateway_port, baud, base_url):
    stop_event = threading.Event()
    packets_forwarded = 0
    lock = threading.Lock()

    def on_collector_line(line):
        print(f"[COLLECTOR] {line}")
        sys.stdout.flush()
        m = RE_COLLECTOR_TX.search(line)
        if not m:
            return
        hex_str = m.group(1).upper()
        if len(hex_str) != FULL_HEX_LEN:
            print(f"[bridge] WARNING: collector hex wrong length ({len(hex_str)} chars), skipping")
            return
        print(f"[bridge] --> LoRa packet from NUCLEO ({len(hex_str)//2} bytes) — waiting for RAK to forward")
        sys.stdout.flush()

    def on_gateway_line(line):
        nonlocal packets_forwarded
        print(f"[GATEWAY]   {line}")
        sys.stdout.flush()
        m = RE_GATEWAY_NBIOT.search(line)
        if not m:
            return
        hex_str = m.group(1).upper()
        resp = post_sms(base_url, hex_str)
        # one retry on failure
        if resp.get("error"):
            time.sleep(1)
            resp = post_sms(base_url, hex_str)
        status  = resp.get("status", resp.get("error", "?"))
        results = resp.get("results", [])
        stored  = any(r.get("stored") for r in results)
        db_tag  = "stored" if stored else "dup"
        with lock:
            packets_forwarded += 1
            count = packets_forwarded
        print(f"[bridge] --> NB-IoT POST #{count}  cloud={status}  {db_tag}")
        sys.stdout.flush()

    collector_thread = threading.Thread(
        target=read_serial,
        args=(collector_port, baud, "COLLECTOR", on_collector_line, stop_event),
        daemon=True,
    )
    gateway_thread = threading.Thread(
        target=read_serial,
        args=(gateway_port, baud, "GATEWAY  ", on_gateway_line, stop_event),
        daemon=True,
    )

    print("=" * 60)
    print("  SenseGate - Full Demo Bridge")
    print("=" * 60)
    print(f"  Collector : NUCLEO on {collector_port}")
    print(f"  Gateway   : RAK4631 on {gateway_port}")
    print(f"  Cloud     : {base_url}/sms")
    print(f"  Pipeline  : NUCLEO --LoRa--> RAK4631 --NB-IoT sim--> cloud")
    print("  Ctrl+C to stop")
    print("=" * 60)
    print()

    collector_thread.start()
    gateway_thread.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[bridge] Stopped by user.")
    finally:
        stop_event.set()

    collector_thread.join(timeout=3)
    gateway_thread.join(timeout=3)
    print(f"[bridge] Done. Total packets forwarded to cloud: {packets_forwarded}")


def main():
    parser = argparse.ArgumentParser(description="SenseGate full demo bridge")
    parser.add_argument("--collector", default="COM3",                  help="NUCLEO serial port (default: COM3)")
    parser.add_argument("--gateway",   default="COM5",                  help="RAK4631 serial port (default: COM5)")
    parser.add_argument("--baud",      type=int, default=115200,        help="Baud rate (default: 115200)")
    parser.add_argument("--url",       default="http://localhost:8080", help="Cloud base URL")
    args = parser.parse_args()

    run(args.collector, args.gateway, args.baud, args.url)


if __name__ == "__main__":
    main()
