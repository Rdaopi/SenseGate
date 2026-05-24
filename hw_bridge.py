"""
SenseGate - Hardware serial bridge

Reads LoRa TX packets from a real NUCLEO board on a serial port and
POSTs them to the local cloud stack at /ingest.

Usage:
    python hw_bridge.py                        # COM3, localhost:8080
    python hw_bridge.py --port COM4
    python hw_bridge.py --port COM3 --url http://localhost:8080
    python hw_bridge.py --port COM3 --device-id 2

Prerequisites:
    - Docker stack running:  cd cloud && docker compose up -d
    - NUCLEO flashed and connected via USB (check Device Manager for COM port)
    - pyserial installed:  pip install pyserial
"""

import argparse
import json
import re
import sys
import time
import urllib.request
import urllib.error

import serial

# Pattern: [HAL COLLECTOR SIM] LoRa TX 50 bytes: <100 hex chars>
RE_LORA_TX = re.compile(
    r"\[HAL (?:COLLECTOR SIM|HW)\] LoRa TX \d+ bytes: ([0-9A-Fa-f]+)"
)

# Also catch the raw TX line format from main.c verbose output
RE_TX_SEQ  = re.compile(r"TX #(\d+)")

PAYLOAD_HEX_LEN = 50  # bytes — only first 25 bytes (50 hex chars) go to /ingest


def http_post(url, payload_dict):
    payload = json.dumps(payload_dict).encode()
    req = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        try:
            body = e.read().decode()
        except Exception:
            body = ""
        return {"error": f"HTTP {e.code}", "body": body}
    except Exception as e:
        return {"error": str(e)}


def post_ingest(base_url, hex_str, device_id, seq):
    return http_post(f"{base_url}/ingest", {
        "hex":       hex_str.upper(),
        "device_id": device_id,
        "sequence":  seq,
        "force":     True,
    })


def run(port, baud, base_url, device_id):
    print(f"[hw_bridge] Opening {port} at {baud} baud...")
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"[hw_bridge] ERROR: cannot open {port}: {e}")
        sys.exit(1)

    print(f"[hw_bridge] Connected. Listening for LoRa TX packets...")
    print(f"[hw_bridge] Cloud: {base_url}/ingest  (force=true)")
    print(f"[hw_bridge] Ctrl+C to stop")
    print()

    seq          = 0
    packets_sent = 0

    try:
        while True:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"[hw_bridge] Serial error: {e}")
                break

            if not raw:
                continue

            try:
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            except Exception:
                continue

            print(f"[HW] {line}")
            sys.stdout.flush()

            # Track sequence from firmware output
            m = RE_TX_SEQ.search(line)
            if m:
                seq = int(m.group(1))
                continue

            # Capture LoRa TX line
            m = RE_LORA_TX.search(line)
            if not m:
                continue

            full_hex = m.group(1).upper()

            # The TX packet is 50 bytes (100 hex chars): current(25) + previous(25).
            # /ingest expects only the first 25 bytes (the current ciphertext).
            if len(full_hex) < PAYLOAD_HEX_LEN * 2:
                print(f"[hw_bridge] WARNING: hex too short ({len(full_hex)} chars), skipping")
                continue

            ciphertext_hex = full_hex[:PAYLOAD_HEX_LEN]  # first 50 hex chars = 25 bytes

            resp = post_ingest(base_url, ciphertext_hex, device_id, seq)
            packets_sent += 1

            status    = resp.get("status", "?")
            fields    = resp.get("fields", {})
            alerts    = resp.get("alerts", [])
            stored    = resp.get("stored", False)
            err       = resp.get("error", "")

            if err:
                tag = f"ERROR: {err}"
            else:
                state_name = fields.get("state_name", "?")
                pallet     = fields.get("pallet_id",  "?")
                wrap       = fields.get("wrap_time",  "?")
                db_tag     = "stored" if stored else "dup"
                alert_tag  = ("  ALERT:" + ",".join(a["type"] for a in alerts)) if alerts else ""
                tag = f"{state_name:<20} pallet={pallet}  wrap={wrap}s  {db_tag}{alert_tag}"

            print(f"[bridge] --> seq={seq:3d}  cloud={status}  {tag}")
            sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[hw_bridge] Stopped by user.")
    finally:
        ser.close()

    print()
    print(f"[hw_bridge] Done. Total packets forwarded: {packets_sent}")


def main():
    parser = argparse.ArgumentParser(description="SenseGate hardware serial bridge")
    parser.add_argument("--port",      default="COM3",                   help="Serial port (default: COM3)")
    parser.add_argument("--baud",      type=int, default=115200,         help="Baud rate (default: 115200)")
    parser.add_argument("--url",       default="http://localhost:8080",  help="Cloud base URL")
    parser.add_argument("--device-id", type=int, default=1,              help="Modbus device ID")
    args = parser.parse_args()

    run(args.port, args.baud, args.url, args.device_id)


if __name__ == "__main__":
    main()
