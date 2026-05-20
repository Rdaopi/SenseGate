"""
SenseGate - QEMU -> Cloud bridge  (+ collector -> gateway LoRa simulation)

The firmware runs forever, emitting one packet per SIM_TX_INTERVAL_MS.
Build with -DSIM_TX_INTERVAL_MS=2000 for a 2-second cadence during testing.
Production cadence is 300000 ms (5 minutes).

Modes:
  1. Collector only (default):
       Runs node_collector QEMU, streams encrypted packets, POSTs to /ingest.
       force=true bypasses dedup so every packet is stored in the DB.

  2. Full pipeline  (--gateway):
       Runs both QEMU nodes in parallel.
       Collector LoRa TX hex is piped into gateway stdin as "LORA_RX <hex>".
       Gateway NB-IoT SMS hex is POSTed to cloud /sms.
       Collector encrypted packets are still POSTed to cloud /ingest directly
       so both paths are visible in Grafana.

Usage:
    python sim_bridge.py [--cloud http://localhost:8080] [--device-id 1]
                         [--gateway]

Prerequisites:
    - Docker stack running:  cd cloud && docker-compose up -d
    - Firmware ELF built with desired SIM_TX_INTERVAL_MS
    - QEMU at C:/Program Files/qemu/qemu-system-arm.exe
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

# -- Paths --------------------------------------------------------------------

REPO_ROOT        = os.path.dirname(os.path.abspath(__file__))
COLLECTOR_ELF    = os.path.join(REPO_ROOT, "node_collector/build/node_collector/zephyr/zephyr.elf")
GATEWAY_ELF      = os.path.join(REPO_ROOT, "node_gateway/build/node_gateway/zephyr/zephyr.elf")
QEMU_EXE         = "C:/Program Files/qemu/qemu-system-arm.exe"

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


# -- Regex patterns -----------------------------------------------------------

RE_SEQ        = re.compile(r"TX #(\d+)")
RE_HEX        = re.compile(r"^\s+([0-9A-Fa-f]{38,100})\s*$")   # 38B collector or 50B gateway
RE_LORA_TX    = re.compile(r"\[HAL COLLECTOR SIM\] LoRa TX \d+ bytes: ([0-9A-Fa-f]+)")
RE_NBIOT_SMS  = re.compile(r"\[HAL GW SIM\] NB-IoT SMS \(\d+ bytes\): ([0-9A-Fa-f]+)")

# -- Cloud calls --------------------------------------------------------------

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
        return {"error": f"HTTP {e.code}", "body": e.read().decode()}
    except Exception as e:
        return {"error": str(e)}

def post_ingest(base_url, hex_str, device_id, seq):
    return http_post(f"{base_url}/ingest", {
        "hex":       hex_str.upper(),
        "device_id": device_id,
        "sequence":  seq,
        "force":     True,
    })

def post_sms(base_url, hex_str):
    payload = urllib.parse.urlencode({"Body": hex_str.upper(), "From": "+sim"}).encode()
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

# -- Gateway QEMU runner ------------------------------------------------------

def run_gateway(gateway_proc, lora_queue, base_url, stop_event):
    """
    Thread target: feeds LoRa packets from lora_queue into gateway stdin,
    reads gateway stdout, POSTs NB-IoT SMS lines to cloud /sms.
    """
    stdin_writer = gateway_proc.stdin

    def feed_stdin():
        while not stop_event.is_set():
            try:
                hex_str = lora_queue.get(timeout=0.5)
                line = f"LORA_RX {hex_str}\n"
                stdin_writer.write(line)
                stdin_writer.flush()
                print(f"[bridge/gw] --> LoRa injected {len(hex_str)//2} bytes into gateway")
                sys.stdout.flush()
            except queue.Empty:
                continue
        stdin_writer.close()

    feeder = threading.Thread(target=feed_stdin, daemon=True)
    feeder.start()

    for raw_line in gateway_proc.stdout:
        line = raw_line.rstrip("\n")
        print(f"[GW] {line}")
        sys.stdout.flush()

        m = RE_NBIOT_SMS.search(line)
        if m:
            hex_str = m.group(1).upper()
            resp    = post_sms(base_url, hex_str)
            status  = resp.get("status", resp.get("error", "?"))
            print(f"[bridge/gw] --> NB-IoT SMS posted  cloud={status}")
            sys.stdout.flush()

    feeder.join(timeout=2)


# -- Collector QEMU runner ----------------------------------------------------

def run_collector(base_url, device_id, lora_queue=None, interval=2.0):
    """
    Spawns collector QEMU. Forwards encrypted packets to /ingest with force=true.
    If lora_queue is provided, also enqueues LoRa TX hex for the gateway.
    Returns number of packets forwarded.
    """
    proc = subprocess.Popen(
        qemu_cmd(COLLECTOR_ELF),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    current_seq     = 0
    expect_hex_next = False
    packets_sent    = 0

    try:
        for raw_line in proc.stdout:
            line = raw_line.rstrip("\n")
            print(f"[COL] {line}")
            sys.stdout.flush()

            # Track sequence number
            m = RE_SEQ.search(line)
            if m:
                current_seq     = int(m.group(1))
                expect_hex_next = False
                continue

            # Next line after "ENCRYPTED AES-128-CTR" is the ciphertext
            if "ENCRYPTED AES-128-CTR" in line:
                expect_hex_next = True
                continue

            if expect_hex_next:
                m = RE_HEX.match(line)
                if m:
                    hex_str         = m.group(1).upper()
                    expect_hex_next = False

                    resp         = post_ingest(base_url, hex_str, device_id, current_seq)
                    packets_sent += 1

                    status     = resp.get("status", "?")
                    fields     = resp.get("fields", {})
                    alerts     = resp.get("alerts", [])
                    stored     = resp.get("stored", False)
                    err        = resp.get("error", "")

                    if err:
                        tag = f"ERROR: {err}"
                    else:
                        state_name = fields.get("state_name", "?")
                        pallet     = fields.get("pallet_id",  "?")
                        wrap       = fields.get("wrap_time",  "?")
                        db_tag     = "stored" if stored else "dup"
                        alert_tag  = ("  ALERT:" + ",".join(a["type"] for a in alerts)) if alerts else ""
                        tag = f"{state_name:<20} pallet={pallet}  wrap={wrap}s  {db_tag}{alert_tag}"

                    print(f"[bridge] --> seq={current_seq:3d}  cloud={status}  {tag}")
                    sys.stdout.flush()
                    time.sleep(interval)
                else:
                    expect_hex_next = False
                continue

            # Forward LoRa TX to gateway queue
            if lora_queue is not None:
                m = RE_LORA_TX.search(line)
                if m:
                    lora_queue.put(m.group(1).upper())

    except KeyboardInterrupt:
        pass
    finally:
        proc.terminate()
        proc.wait()

    return packets_sent


# -- Main ---------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="SenseGate QEMU -> Cloud bridge")
    parser.add_argument("--cloud",     default="http://localhost:8080", help="Flask sim base URL")
    parser.add_argument("--device-id", type=int, default=1,            help="Modbus device ID")
    parser.add_argument("--gateway",   action="store_true",            help="Also run gateway QEMU (full pipeline)")
    parser.add_argument("--interval",  type=float, default=2.0,        help="Seconds between packets (real time)")
    args = parser.parse_args()

    if not os.path.exists(COLLECTOR_ELF):
        print(f"[bridge] ERROR: collector ELF not found: {COLLECTOR_ELF}")
        print("[bridge] Build: cd node_collector && west build -b qemu_cortex_m3 -- -DSIM_TX_INTERVAL_MS=2000")
        sys.exit(1)

    if args.gateway and not os.path.exists(GATEWAY_ELF):
        print(f"[bridge] ERROR: gateway ELF not found: {GATEWAY_ELF}")
        print("[bridge] Build: cd node_gateway && west build -b qemu_cortex_m3")
        sys.exit(1)

    if not os.path.exists(QEMU_EXE):
        print(f"[bridge] ERROR: QEMU not found: {QEMU_EXE}")
        sys.exit(1)

    print("=" * 60)
    print("  SenseGate - QEMU -> Cloud bridge")
    print("=" * 60)
    print(f"  Collector ELF : {COLLECTOR_ELF}")
    if args.gateway:
        print(f"  Gateway ELF   : {GATEWAY_ELF}")
    print(f"  Cloud         : {args.cloud}/ingest  (force=true)")
    print(f"  DevID         : {args.device_id}")
    if args.gateway:
        print(f"  Pipeline      : collector -> LoRa -> gateway -> NB-IoT -> cloud")
    print(f"  Interval      : {args.interval}s between packets (real time)")
    print("  Firmware loops forever — Ctrl+C to stop")
    print("=" * 60)
    print()

    lora_queue = None
    gw_thread  = None
    gw_proc    = None
    stop_event = threading.Event()

    if args.gateway:
        lora_queue = queue.Queue()
        gw_proc = subprocess.Popen(
            qemu_cmd(GATEWAY_ELF),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        gw_thread = threading.Thread(
            target=run_gateway,
            args=(gw_proc, lora_queue, args.cloud, stop_event),
            daemon=True,
        )
        gw_thread.start()

    sent = 0
    try:
        sent = run_collector(args.cloud, args.device_id, lora_queue, args.interval)
    except KeyboardInterrupt:
        print("\n[bridge] Stopped by user.")

    if args.gateway:
        stop_event.set()
        if gw_proc:
            gw_proc.terminate()
            try:
                gw_proc.wait(timeout=3)
            except Exception:
                gw_proc.kill()
        if gw_thread:
            gw_thread.join(timeout=3)

    print()
    print(f"[bridge] Done. Total packets forwarded to cloud: {sent}")


if __name__ == "__main__":
    main()
