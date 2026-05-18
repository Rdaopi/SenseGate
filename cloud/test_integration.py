"""
SenseGate — Full integration test
Requires the Docker stack running at localhost:8080 / localhost:3000.
DB must be accessible on localhost:5432 (docker-compose exposes it).

Run: python test_integration.py
Exits 0 if all checks pass, 1 if any fail.

Test design:
  - Seed (sequences 0-13) is pre-loaded by Docker init scripts (14 rows)
  - This test sends a fresh batch (sequences 100-113) via /ingest
  - After ingestion: 14 seed + 14 new = 28 rows total
  - Anomaly sequences (109-111 = ALARM, 112-113 = OFFLINE) must fire alerts
"""

import json
import os
import sys
import time
import urllib.request
import urllib.error

BASE = "http://localhost:8080"

# Sequences 100-113: a second production run, encrypted with same key
# Generated with the same firmware pack() + AES-CTR
INGEST_PACKETS = [
    {"seq": 100, "hex": "EF1F34E408BACBAD878AB70DC69C3B4998FC4673E4F16C00CE", "state": 30, "pallet": 260000, "wrap": 668,   "alerts_min": 0},
    {"seq": 101, "hex": "35EA3D6E1524FB6D263FEC8DD5CAF58C49D577019FF5337392", "state": 30, "pallet": 259989, "wrap": 2717,  "alerts_min": 0},
    {"seq": 102, "hex": "85DC0C73EA7766B379F9D09BFBAD61E16F28F4CF5F745C43BF", "state": 30, "pallet": 259978, "wrap": 4766,  "alerts_min": 0},
    {"seq": 103, "hex": "CDAC2F0E923EF3FE90DA89295914D9D7B8DBEC496009ECD76F", "state": 30, "pallet": 259967, "wrap": 6815,  "alerts_min": 0},
    {"seq": 104, "hex": "60A827B11C1D644E9CE6119E9ADB552F5E2BB9B570C2589057", "state": 30, "pallet": 259956, "wrap": 8864,  "alerts_min": 0},
    {"seq": 105, "hex": "26D3BA58441D453C61FBA3A24FEFEE7B022C756D4322D78643", "state": 30, "pallet": 259945, "wrap": 10913, "alerts_min": 0},
    {"seq": 106, "hex": "3D03A4C249F61A53E0D8E4E21DCA06A57D5D5BEE1F4465866C", "state": 30, "pallet": 259934, "wrap": 12962, "alerts_min": 0},
    {"seq": 107, "hex": "6A8E1E384708151C02A6A0B915D893D8C2B3F9CD3E2BC05E87", "state": 30, "pallet": 259923, "wrap": 15011, "alerts_min": 0},
    {"seq": 108, "hex": "2335F330E23C9BB9071804D44288A3C93B27D0213A5B91236E", "state": 30, "pallet": 259912, "wrap": 17060, "alerts_min": 0},
    {"seq": 109, "hex": "DFDEE7EF04260FE8BD61EAF6D07616E1B990515C293BDDF68E", "state": 31, "pallet": 259900, "wrap": 19107, "alerts_min": 1},
    {"seq": 110, "hex": "4BA8F666CE9F37EECD2F362E40781990472BC00A0C7D8FDA56", "state": 31, "pallet": 259890, "wrap": 19107, "alerts_min": 1},
    {"seq": 111, "hex": "8DA2FE04A429012992C537631692BFFDDEC1DF58D916E1482B", "state": 31, "pallet": 259880, "wrap": 19107, "alerts_min": 1},
    {"seq": 112, "hex": "84DDC94274E937E39EC1197BD5033CB3310FD5C480157BC7BC", "state": 99, "pallet": 259000, "wrap": 0,     "alerts_min": 1},
    {"seq": 113, "hex": "1D731CBB27FE5AA05C142ED2476FA0B7743C34F60A292D0587", "state": 99, "pallet": 258995, "wrap": 0,     "alerts_min": 1},
]

STATE_LABEL = {30: "RUN", 31: "ALARM", 99: "OFFLINE"}


def _post_json(url: str, payload: dict) -> dict:
    data = json.dumps(payload).encode()
    req = urllib.request.Request(
        url, data=data, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


def _get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=10) as resp:
        return json.loads(resp.read())


def _ensure_seed():
    """Verify seed rows 0-13 are present; re-ingest them if missing."""
    resp = _get_json(f"{BASE}/readings?limit=100")
    seqs_in_db = {r["sequence"] for r in resp.get("readings", [])}
    missing = [s for s in range(14) if s not in seqs_in_db]
    if missing:
        print(f"  Re-ingesting missing seed sequences: {missing}")
        SEED_HEX = [
            "436E149A1C324E9D55A5AD34DDB5CB3E97E27025C748ACCD1A",
            "E76FBD556E3B33DC54D2FFBA6F16DA1E4D38F09EB276539F8C",
            "CD7E1A31B063A5909C6725E270743BF2701318AB713C0A7776",
            "5AB8E0B2668C34473B6886194B0388EF383125DA90A81552D0",
            "D7117F320ABDBFFAF704AF10CAB9528CC8DE6060C8F3CC8883",
            "37F3832FC6865E417447E17992B71045E4B7DF266CDC4C5A28",
            "42F4A6E15DF949D0DBD921EE6AB5DC6C110ADC1A76999074A1",
            "1239F6E57370DC33C006484171C9AF8C560DABEAC795A81A5D",
            "F3989641CF8881E18AEBA65F1B9E0093939DD07D3ECDF264BB",
            "C9C34588AEB97FC945671BEF6943FB099925C2DA4A26B3A285",
            "7C986454D918347515D009A464723E24D9F355BB8D81D949A5",
            "ACD3D259BB6F60D03818B21BF659D69573CD5E518D6106E41A",
            "03403C53F6BFFCAC092DA414C0D75AF651AC7646B64FD31E7F",
            "236CC09133719254D107951E105714E5C047670E15DFA4C07B",
        ]
        for seq in missing:
            _post_json(
                f"{BASE}/ingest",
                {"hex": SEED_HEX[seq], "device_id": 1, "sequence": seq},
            )
    resp2 = _get_json(f"{BASE}/readings?limit=100")
    return resp2.get("count", 0)


all_pass = True

# ── Pre-check: health ─────────────────────────────────────────────────────────
print("Checking service health ...")
try:
    r = _get_json(f"{BASE}/health")
    assert r.get("status") == "ok"
    print("  Lambda: OK")
except Exception as exc:
    print(f"  Lambda health FAILED: {exc}")
    all_pass = False

try:
    r = _get_json("http://localhost:3000/api/health")
    assert r.get("database") == "ok"
    print("  Grafana: OK")
except Exception as exc:
    print(f"  Grafana health FAILED: {exc}")
    all_pass = False

# ── Ensure seed is present (14 rows, seq 0-13) ────────────────────────────────
print()
print("Ensuring seed data (sequences 0-13) ...")
seed_rows = _ensure_seed()
seed_ok = seed_rows >= 14
print(f"  seed rows: {seed_rows}  {'OK' if seed_ok else 'MISSING'}")
if not seed_ok:
    all_pass = False

# ── Step 1: ingest 14 new packets (sequences 100-113) ─────────────────────────
print()
print("Sending 14 packets via /ingest (seq 100-113) ...")
print()
print(f"{'Vector':6}  {'Seq':3}  {'State':7}  {'Pallet':7}  {'WrapTime':8}  {'Alerts':6}  {'DB':5}  {'Status':7}")
print(f"{'------':6}  {'---':3}  {'-------':7}  {'-------':7}  {'--------':8}  {'------':6}  {'-----':5}  {'-------':7}")

ingest_results = []
for i, pkt in enumerate(INGEST_PACKETS):
    try:
        resp = _post_json(
            f"{BASE}/ingest",
            {"hex": pkt["hex"], "device_id": 1, "sequence": pkt["seq"]},
        )
    except Exception as exc:
        print(f"{i:6}  {pkt['seq']:3}  ERROR: {exc}")
        all_pass = False
        ingest_results.append(None)
        continue

    ok       = resp.get("status") == "ok"
    fields   = resp.get("fields", {})
    alerts   = resp.get("alerts", [])
    stored   = resp.get("stored", False)

    state_ok   = fields.get("state") == pkt["state"]
    pallet_ok  = fields.get("pallet_id") == pkt["pallet"]
    wrap_ok    = fields.get("wrap_time") == pkt["wrap"]
    alerts_ok  = len(alerts) >= pkt["alerts_min"]

    row_pass = ok and state_ok and pallet_ok and wrap_ok and alerts_ok
    if not row_pass:
        all_pass = False

    status_str = "PASS" if row_pass else "FAIL"
    state_str  = STATE_LABEL.get(pkt["state"], str(pkt["state"]))
    db_str     = "stored" if stored else "dup"

    print(f"{i:6}  {pkt['seq']:3}  {state_str:7}  {pkt['pallet']:7}  {pkt['wrap']:8}s  {len(alerts):6}  {db_str:5}  {status_str:7}", end="")
    if not row_pass:
        if not state_ok:  print(f"  state got={fields.get('state')}", end="")
        if not pallet_ok: print(f"  pallet got={fields.get('pallet_id')}", end="")
        if not wrap_ok:   print(f"  wrap got={fields.get('wrap_time')}", end="")
        if not alerts_ok: print(f"  alerts got={len(alerts)} expected>={pkt['alerts_min']}", end="")
    print()
    ingest_results.append(resp)

# ── Step 2: verify /readings total >= 28 ─────────────────────────────────────
print()
print("Checking /readings row count (seed 14 + new 14 = 28) ...")
readings_resp = _get_json(f"{BASE}/readings?limit=200")
total_rows = readings_resp.get("count", 0)
readings_pass = total_rows >= 28
if not readings_pass:
    all_pass = False
print(f"  rows in DB: {total_rows}  (expected >= 28)  {'PASS' if readings_pass else 'FAIL'}")

# ── Step 3: verify /summary is non-empty ─────────────────────────────────────
print()
print("Checking /summary ...")
summary_resp = _get_json(f"{BASE}/summary")
summary_count = summary_resp.get("count", 0)
summary_pass  = summary_count > 0
if not summary_pass:
    all_pass = False
print(f"  hourly buckets: {summary_count}  {'PASS' if summary_pass else 'FAIL'}")

# ── Step 4: verify alert packets ─────────────────────────────────────────────
print()
print("Checking alert packets (seq 109-113 should fire alerts) ...")
for i, pkt in enumerate(INGEST_PACKETS):
    if pkt["alerts_min"] > 0 and ingest_results[i] is not None:
        alerts = ingest_results[i].get("alerts", [])
        ok = len(alerts) >= pkt["alerts_min"]
        state_str = STATE_LABEL.get(pkt["state"], str(pkt["state"]))
        print(f"  seq={pkt['seq']:3d}  {state_str:7}  alerts={len(alerts)}  {'PASS' if ok else 'FAIL'}")
        if not ok:
            all_pass = False

# ── Step 5: verify Grafana dashboard ─────────────────────────────────────────
print()
print("Checking Grafana dashboard ...")
try:
    dash_resp = _get_json("http://localhost:3000/api/dashboards/uid/sensegate-main")
    panels = len(dash_resp.get("dashboard", {}).get("panels", []))
    dash_pass = panels >= 5
    print(f"  panels: {panels}  {'PASS' if dash_pass else 'FAIL'}")
    if not dash_pass:
        all_pass = False
except Exception as exc:
    print(f"  Grafana FAILED: {exc}")
    all_pass = False

# ── Result ────────────────────────────────────────────────────────────────────
print()
if all_pass:
    print("ALL INTEGRATION CHECKS PASS")
    sys.exit(0)
else:
    print("SOME INTEGRATION CHECKS FAILED")
    sys.exit(1)
