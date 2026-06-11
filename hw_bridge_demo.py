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

# Nordic UART Service TX characteristic — the RAK notifies SMS frames on this
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

GATEWAY_NAME = "SenseGate-GW"
DEBUG = False  # set via --debug flag

# Require exactly 100 hex chars (50-byte frame) so partial BLE frames are
# never consumed — they remain in the buffer until the rest arrives.
RE_GATEWAY_NBIOT = re.compile(
    r"SMS:([0-9A-Fa-f]{100})"
)


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


async def sms_consumer(base_url, queue, counter):
    """Single consumer: POST packets in arrival order, one in flight at a time.

    Serializing the POSTs preserves FIFO order, provides backpressure (the
    queue absorbs bursts while a slow POST is in flight), and keeps every
    exception visible — nothing is fire-and-forget.
    """
    loop = asyncio.get_event_loop()
    while True:
        hex_str = await queue.get()
        try:
            # post_sms is blocking urllib — run it in the default executor
            # so the BLE event loop stays free.
            resp = await loop.run_in_executor(None, post_sms, base_url, hex_str)
            if resp.get("error"):
                await asyncio.sleep(1)
                resp = await loop.run_in_executor(None, post_sms, base_url, hex_str)
            status  = resp.get("status", resp.get("error", "?"))
            results = resp.get("results", [])
            stored  = any(r.get("stored") for r in results)
            db_tag  = "stored" if stored else "dup"
            counter[0] += 1
            print(f"[bridge] --> NB-IoT POST #{counter[0]}  cloud={status}  {db_tag}")
            sys.stdout.flush()
        except asyncio.CancelledError:
            raise
        except Exception as e:
            print(f"[bridge] ERROR forwarding packet: {e}")
            sys.stdout.flush()
        finally:
            queue.task_done()


async def ble_gateway_task(queue, stop_event):
    """Scan for SenseGate-GW, connect, push received SMS frames onto the queue."""
    loop = asyncio.get_event_loop()
    while not stop_event.is_set():
        print(f"[BLE] Scanning for '{GATEWAY_NAME}'...")
        try:
            device = await BleakScanner.find_device_by_name(GATEWAY_NAME, timeout=10.0)
        except asyncio.CancelledError:
            raise
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
            # Extract complete SMS packets (exactly 100 hex chars) from raw buffer.
            while True:
                m = RE_GATEWAY_NBIOT.search(buf)
                if not m:
                    break
                hex_str = m.group(1).upper()
                if DEBUG:
                    print(f"[GATEWAY/BLE] SMS received (100 chars)")
                    sys.stdout.flush()
                # Hand off to the consumer task; call_soon_threadsafe is safe
                # regardless of which thread the BLE backend invokes us on.
                loop.call_soon_threadsafe(queue.put_nowait, hex_str)
                buf = buf[m.end():]
            # Trim buffer: preserve from the last 'SMS:' prefix so a partial frame
            # that straddles the boundary is never discarded.
            if len(buf) > 512:
                last_sms = buf.rfind("SMS:")
                buf = buf[last_sms:] if last_sms >= 0 else buf[-256:]

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
        except asyncio.CancelledError:
            raise
        except Exception as e:
            # If we're shutting down, don't swallow a cancellation that was
            # replaced by an exception raised during BleakClient teardown.
            if stop_event.is_set():
                break
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
        queue = asyncio.Queue()
        consumer = asyncio.create_task(sms_consumer(base_url, queue, counter))
        ble_task = asyncio.create_task(ble_gateway_task(queue, stop_event))
        try:
            await ble_task
        except asyncio.CancelledError:
            pass
        finally:
            consumer.cancel()

    async def shutdown():
        """Cancel pending tasks (excluding ourselves) and let them clean up."""
        tasks = [t for t in asyncio.all_tasks() if t is not asyncio.current_task()]
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    try:
        loop.run_until_complete(main_loop())
    except KeyboardInterrupt:
        print("\n[bridge] Stopped by user.")
        stop_event.set()
        loop.run_until_complete(shutdown())
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
