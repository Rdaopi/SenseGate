"""
SenseGate — QEMU → Cloud bridge

Runs node_collector under QEMU, captures its stdout, and for every
encrypted packet printed by the firmware it POSTs to the local Flask sim.

Usage:
    python sim_bridge.py [--cloud http://localhost:8080] [--device-id 1]

Requirements:
    - nRF Connect SDK at C:/ncs/v2.9.0  (for QEMU binary)
    - Docker stack running (docker-compose up -d in cloud/)
    - node_collector already built (build/node_collector/zephyr/zephyr.elf)

What it does:
    1. Spawns QEMU with zephyr.elf, captures stdout line by line
    2. Parses "TX #<seq>" lines to track the current sequence number
    3. Parses the hex line after "ENCRYPTED AES-128-CTR" — that is the
       25-byte ciphertext produced by the firmware
    4. POSTs {"hex": "...", "device_id": 1, "sequence": N} to /ingest
    5. Prints the cloud response alongside the firmware output
"""

import argparse
import json
import re
import subprocess
import sys
import urllib.request
import urllib.error

# ── Config ────────────────────────────────────────────────────────────────────

QEMU_BIN  = "C:/ncs/v2.9.0/zephyr/boards/qemu/cortex_m3/../../../scripts/../.."
# Actual QEMU bundled with nRF Connect SDK
QEMU_EXE  = "C:/ncs/toolchains/b620d30767/opt/zephyr-sdk/sysroots/x86_64-pokysdk-mingw32/usr/bin/qemu-system-arm"
ELF_PATH  = "node_collector/build/node_collector/zephyr/zephyr.elf"

QEMU_ARGS = [
    QEMU_EXE,
    "-cpu",     "cortex-m3",
    "-machine", "lm3s6965evb",
    "-nographic",
    "-kernel",  ELF_PATH,
]

# ── Regex patterns matching main.c printk output ──────────────────────────────

# "  TX #9  [Anomaly - pallet stuck]"
RE_SEQ = re.compile(r"TX #(\d+)")

# Line after "  ENCRYPTED AES-128-CTR (25 bytes)" — just the hex
RE_HEX = re.compile(r"^  ([0-9A-F]{50})$")

# ── Cloud POST ────────────────────────────────────────────────────────────────

def post_ingest(base_url: str, hex_str: str, device_id: int, seq: int) -> dict:
    payload = json.dumps({"hex": hex_str, "device_id": device_id, "sequence": seq}).encode()
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
    parser = argparse.ArgumentParser()
    parser.add_argument("--cloud",     default="http://localhost:8080")
    parser.add_argument("--device-id", type=int, default=1)
    args = parser.parse_args()

    print(f"[bridge] Starting QEMU: {ELF_PATH}")
    print(f"[bridge] Cloud endpoint: {args.cloud}/ingest")
    print(f"[bridge] Device ID: {args.device_id}")
    print()

    try:
        proc = subprocess.Popen(
            QEMU_ARGS,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
    except FileNotFoundError:
        print(f"[bridge] ERROR: QEMU not found at {QEMU_EXE}")
        print("[bridge] Check QEMU_EXE path in this script.")
        sys.exit(1)

    current_seq     = 0
    expect_hex_next = False   # True when the next hex-only line is the ciphertext
    packets_sent    = 0

    try:
        for raw_line in proc.stdout:
            line = raw_line.rstrip("\n")
            sys.stdout.write(line + "\n")
            sys.stdout.flush()

            # Track sequence number from "TX #N" header
            m = RE_SEQ.search(line)
            if m:
                current_seq = int(m.group(1))
                expect_hex_next = False
                continue

            # The line "  ENCRYPTED AES-128-CTR (25 bytes)" signals that
            # the very next content line holds the hex
            if "ENCRYPTED AES-128-CTR" in line:
                expect_hex_next = True
                continue

            # Capture the hex line that follows
            if expect_hex_next:
                m = RE_HEX.match(line)
                if m:
                    hex_str = m.group(1)
                    expect_hex_next = False

                    resp = post_ingest(args.cloud, hex_str, args.device_id, current_seq)
                    packets_sent += 1

                    status     = resp.get("status", "?")
                    state_name = resp.get("fields", {}).get("state_name", "?")
                    pallet     = resp.get("fields", {}).get("pallet_id", "?")
                    wrap       = resp.get("fields", {}).get("wrap_time", "?")
                    alerts     = resp.get("alerts", [])
                    stored     = resp.get("stored", False)

                    alert_str = ""
                    if alerts:
                        alert_str = "  ALERT: " + ", ".join(a["type"] for a in alerts)

                    db_str = "stored" if stored else "dup"
                    print(
                        f"[bridge] seq={current_seq:3d}  {state_name:<20}  "
                        f"pallet={pallet}  wrap={wrap}s  "
                        f"cloud={status}/{db_str}{alert_str}"
                    )
                    sys.stdout.flush()
                else:
                    # hex wasn't on the very next line — reset flag
                    expect_hex_next = False

    except KeyboardInterrupt:
        print("\n[bridge] Interrupted by user")
    finally:
        proc.terminate()
        proc.wait()

    print(f"\n[bridge] Done. Packets forwarded to cloud: {packets_sent}")


if __name__ == "__main__":
    main()
