"""
SenseGate - Hardware bridge: NUCLEO collector -> gateway QEMU -> cloud

Full pipeline simulation using real NUCLEO hardware as collector:

    PC (modbus_sim)
      -> NUCLEO (AES, pack, serial COM3)
        -> hw_bridge.py
          -> gateway QEMU stdin ("LORA_RX <hex>")
            -> NB-IoT simulated -> /sms -> cloud -> Grafana

Usage:
    python hw_bridge.py --port COM3
    python hw_bridge.py --port COM5 --url http://localhost:8080

Prerequisites:
    - Docker stack running:  cd cloud && docker compose up -d
    - NUCLEO flashed and connected via USB
    - node_gateway built for QEMU:  cd node_gateway && west build -b qemu_cortex_m3
    - pyserial installed:  pip install pyserial
"""

import argparse
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
import urllib.error

import serial

REPO_ROOT    = os.path.dirname(os.path.abspath(__file__))
GATEWAY_ELF  = os.path.join(REPO_ROOT, "node_gateway/build/node_gateway/zephyr/zephyr.elf")
QEMU_EXE     = "C:/Program Files/qemu/qemu-system-arm.exe"

RE_LORA_TX = re.compile(
    r"\[HAL (?:COLLECTOR SIM|HW)\] LoRa TX 50 bytes: ([0-9A-Fa-f]+)"
)
RE_TX_SEQ  = re.compile(r"TX #(\d+)\s+\[")
RE_NBIOT   = re.compile(r"\[HAL GW SIM\] NB-IoT SMS \(\d+ bytes\): ([0-9A-Fa-f]+)")

FULL_HEX_LEN = 100   # 50 bytes * 2 hex chars


def qemu_cmd(elf_path):
    return [
        QEMU_EXE,
        "-cpu",      "cortex-m3",
        "-machine",  "lm3s6965evb",
        "-nographic",
        "-vga",      "none",
        "-net",      "none",
        "-chardev",  "stdio,id=con,mux=on",
        "-serial",   "chardev:con",
        "-mon",      "chardev=con,mode=readline",
        "-icount",   "shift=6,align=off,sleep=off",
        "-rtc",      "clock=vm",
        "-kernel",   elf_path,
    ]


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


def run_gateway(gw_proc, lora_queue, base_url, stop_event):
    """Thread: feeds LoRa packets from queue into gateway stdin, POSTs SMS to /sms."""

    def feed_stdin():
        while not stop_event.is_set():
            try:
                hex_str = lora_queue.get(timeout=0.5)
                line = f"LORA_RX {hex_str}\n"
                gw_proc.stdin.write(line)
                gw_proc.stdin.flush()
                print(f"[bridge/gw] --> LoRa injected {len(hex_str)//2} bytes into gateway")
                sys.stdout.flush()
            except queue.Empty:
                continue
        gw_proc.stdin.close()

    feeder = threading.Thread(target=feed_stdin, daemon=True)
    feeder.start()

    for raw_line in gw_proc.stdout:
        line = raw_line.rstrip("\n")
        print(f"[GW] {line}")
        sys.stdout.flush()

        m = RE_NBIOT.search(line)
        if m:
            hex_str = m.group(1).upper()
            resp    = post_sms(base_url, hex_str)
            if resp.get("error"):
                time.sleep(1)
                resp = post_sms(base_url, hex_str)
            status  = resp.get("status", resp.get("error", "?"))
            results = resp.get("results", [])
            stored  = any(r.get("stored") for r in results)
            db_tag  = "stored" if stored else "dup"
            print(f"[bridge/gw] --> NB-IoT SMS posted  cloud={status}  {db_tag}")
            sys.stdout.flush()

    feeder.join(timeout=2)


def run(port, baud, base_url):
    # Validate gateway ELF
    if not os.path.exists(GATEWAY_ELF):
        print(f"[hw_bridge] ERROR: gateway ELF not found: {GATEWAY_ELF}")
        print("[hw_bridge] Build: cd node_gateway && west build -b qemu_cortex_m3")
        sys.exit(1)

    if not os.path.exists(QEMU_EXE):
        print(f"[hw_bridge] ERROR: QEMU not found: {QEMU_EXE}")
        sys.exit(1)

    print(f"[hw_bridge] Opening {port} at {baud} baud...")
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"[hw_bridge] ERROR: cannot open {port}: {e}")
        sys.exit(1)

    print(f"[hw_bridge] Starting gateway QEMU...")
    gw_proc = subprocess.Popen(
        qemu_cmd(GATEWAY_ELF),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    lora_queue = queue.Queue()
    stop_event = threading.Event()
    gw_thread  = threading.Thread(
        target=run_gateway,
        args=(gw_proc, lora_queue, base_url, stop_event),
        daemon=True,
    )
    gw_thread.start()

    print("=" * 60)
    print("  SenseGate - HW bridge")
    print("=" * 60)
    print(f"  Collector : NUCLEO on {port} (real hardware)")
    print(f"  Gateway   : QEMU ({GATEWAY_ELF})")
    print(f"  Cloud     : {base_url}/sms")
    print(f"  Pipeline  : NUCLEO -> LoRa sim -> gateway QEMU -> NB-IoT sim -> cloud")
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

            # Inject full 50-byte packet into gateway QEMU stdin
            lora_queue.put(full_hex)
            packets_sent += 1
            print(f"[bridge] --> seq={seq:3d}  LoRa packet queued for gateway")
            sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[hw_bridge] Stopped by user.")
    finally:
        ser.close()
        stop_event.set()
        gw_proc.terminate()
        try:
            gw_proc.wait(timeout=3)
        except Exception:
            gw_proc.kill()
        gw_thread.join(timeout=3)

    print()
    print(f"[hw_bridge] Done. Total packets sent to gateway: {packets_sent}")


def main():
    parser = argparse.ArgumentParser(description="SenseGate HW bridge: NUCLEO -> gateway QEMU -> cloud")
    parser.add_argument("--port",  default="COM3",                  help="Serial port of NUCLEO (default: COM3)")
    parser.add_argument("--baud",  type=int, default=115200,        help="Baud rate (default: 115200)")
    parser.add_argument("--url",   default="http://localhost:8080", help="Cloud base URL")
    args = parser.parse_args()

    run(args.port, args.baud, args.url)


if __name__ == "__main__":
    main()
