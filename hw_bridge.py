"""
SenseGate - Hardware bridge: NUCLEO collector -> cloud (direct)

Pipeline:
    NUCLEO (AES, pack, LoRa TX via Ebyte E22)
      -> hw_bridge.py reads serial port
        -> POST /sms -> Docker -> Grafana

Usage:
    python hw_bridge.py --port COM3
    python hw_bridge.py --port COM3 --url http://localhost:8080

Prerequisites:
    - Docker stack running:  cd cloud && docker compose up -d
    - NUCLEO flashed and connected via USB
    - pyserial installed:  pip install pyserial
"""

import argparse
import json
import re
import sys
import urllib.parse
import urllib.request
import urllib.error

import serial

RE_LORA_TX = re.compile(
    r"\[HAL (?:COLLECTOR SIM|COLLECTOR HW)\] LoRa TX 50 bytes: ([0-9A-Fa-f]+)"
)
RE_TX_SEQ = re.compile(r"TX #(\d+)\s+\[")

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


def run(port, baud, base_url):
    print(f"[hw_bridge] Opening {port} at {baud} baud...")
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"[hw_bridge] ERROR: cannot open {port}: {e}")
        sys.exit(1)

    print("=" * 60)
    print("  SenseGate - HW bridge (direct)")
    print("=" * 60)
    print(f"  Collector : NUCLEO on {port} (real hardware)")
    print(f"  Cloud     : {base_url}/sms")
    print(f"  Pipeline  : NUCLEO -> bridge -> /sms -> cloud -> Grafana")
    print("  Ctrl+C to stop")
    print("=" * 60)
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

            m = RE_TX_SEQ.search(line)
            if m:
                seq = int(m.group(1))
                continue

            m = RE_LORA_TX.search(line)
            if not m:
                continue

            full_hex = m.group(1).upper()

            if len(full_hex) != FULL_HEX_LEN:
                print(f"[hw_bridge] WARNING: hex wrong length ({len(full_hex)} chars), skipping")
                continue

            resp    = post_sms(base_url, full_hex)
            status  = resp.get("status", resp.get("error", "?"))
            results = resp.get("results", [])
            stored  = any(r.get("stored") for r in results)
            db_tag  = "stored" if stored else "dup/err"
            packets_sent += 1
            print(f"[bridge] --> seq={seq:3d}  SMS posted  cloud={status}  {db_tag}")
            sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[hw_bridge] Stopped by user.")
    finally:
        ser.close()

    print()
    print(f"[hw_bridge] Done. Total packets sent to cloud: {packets_sent}")


def main():
    parser = argparse.ArgumentParser(description="SenseGate HW bridge: NUCLEO -> cloud")
    parser.add_argument("--port", default="COM3",                  help="Serial port of NUCLEO (default: COM3)")
    parser.add_argument("--baud", type=int, default=115200,        help="Baud rate (default: 115200)")
    parser.add_argument("--url",  default="http://localhost:8080", help="Cloud base URL")
    args = parser.parse_args()

    run(args.port, args.baud, args.url)


if __name__ == "__main__":
    main()
