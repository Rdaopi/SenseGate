"""
SenseGate — QEMU → Cloud bridge

Runs node_collector firmware under QEMU, captures its stdout in real time,
and POSTs every encrypted packet to the local Docker cloud simulation.

Usage:
    python sim_bridge.py [--cloud http://localhost:8080] [--device-id 1]

Prerequisites:
    - Docker stack running:  cd cloud && docker-compose up -d
    - Firmware already built under node_collector/build/node_collector/
    - QEMU at C:/Program Files/qemu/qemu-system-arm.exe
"""

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.request
import urllib.error

# ── Paths ─────────────────────────────────────────────────────────────────────

REPO_ROOT  = os.path.dirname(os.path.abspath(__file__))
ELF_PATH   = os.path.join(REPO_ROOT, "node_collector/build/node_collector/zephyr/zephyr.elf")
QEMU_EXE   = "C:/Program Files/qemu/qemu-system-arm.exe"

# Exact flags from the Zephyr board.cmake for qemu_cortex_m3/lm3s6965
QEMU_CMD = [
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
    "-kernel",   ELF_PATH,
]

# ── Regex patterns — match main.c printk lines ────────────────────────────────
#   "  TX #9  [Anomaly - pallet stuck]"
RE_SEQ = re.compile(r"TX #(\d+)")
#   line printed after "  ENCRYPTED AES-128-CTR (25 bytes)\n"
#   the hex is on its own line, prefixed with two spaces: "  AABBCC..."
RE_HEX = re.compile(r"^\s+([0-9A-Fa-f]{50})\s*$")

# ── Cloud POST ────────────────────────────────────────────────────────────────

def post_ingest(base_url: str, hex_str: str, device_id: int, seq: int) -> dict:
    payload = json.dumps({
        "hex":       hex_str.upper(),
        "device_id": device_id,
        "sequence":  seq,
    }).encode()
    req = urllib.request.Request(
        f"{base_url}/ingest",
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

# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="SenseGate QEMU→Cloud bridge")
    parser.add_argument("--cloud",     default="http://localhost:8080", help="Flask sim base URL")
    parser.add_argument("--device-id", type=int, default=1,            help="Modbus device ID")
    args = parser.parse_args()

    if not os.path.exists(ELF_PATH):
        print(f"[bridge] ERROR: ELF not found: {ELF_PATH}")
        print("[bridge] Build the firmware first: cd node_collector && west build -b qemu_cortex_m3")
        sys.exit(1)

    if not os.path.exists(QEMU_EXE):
        print(f"[bridge] ERROR: QEMU not found: {QEMU_EXE}")
        sys.exit(1)

    print("=" * 60)
    print("  SenseGate - QEMU -> Cloud bridge")
    print("=" * 60)
    print(f"  ELF   : {ELF_PATH}")
    print(f"  Cloud : {args.cloud}/ingest")
    print(f"  DevID : {args.device_id}")
    print("=" * 60)
    print()

    try:
        proc = subprocess.Popen(
            QEMU_CMD,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
    except Exception as e:
        print(f"[bridge] ERROR launching QEMU: {e}")
        sys.exit(1)

    current_seq     = 0
    expect_hex_next = False
    packets_sent    = 0

    try:
        for raw_line in proc.stdout:
            line = raw_line.rstrip("\n")

            # Echo firmware output as-is
            print(line)
            sys.stdout.flush()

            # Track "TX #N" — sequence for the next ciphertext
            m = RE_SEQ.search(line)
            if m:
                current_seq     = int(m.group(1))
                expect_hex_next = False
                continue

            # The "ENCRYPTED AES-128-CTR" label line — hex follows on next line
            if "ENCRYPTED AES-128-CTR" in line:
                expect_hex_next = True
                continue

            # Capture the hex ciphertext line
            if expect_hex_next:
                m = RE_HEX.match(line)
                if m:
                    hex_str         = m.group(1).upper()
                    expect_hex_next = False

                    resp       = post_ingest(args.cloud, hex_str, args.device_id, current_seq)
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
                else:
                    expect_hex_next = False

    except KeyboardInterrupt:
        print("\n[bridge] Stopped by user.")
    finally:
        proc.terminate()
        proc.wait()

    print()
    print(f"[bridge] Done. Total packets forwarded to cloud: {packets_sent}")


if __name__ == "__main__":
    main()
