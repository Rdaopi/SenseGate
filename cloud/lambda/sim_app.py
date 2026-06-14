"""
SenseGate — Local Docker simulation server
Exposes: /health, /ingest, /sms, /readings, /summary
Wraps the real payload_parser + aes_decrypt logic.
"""

import json
import logging
import os
import sys
import urllib.parse

import psycopg2
import psycopg2.extras
from flask import Flask, jsonify, request

sys.path.insert(0, os.path.dirname(__file__))
from aes_decrypt import decrypt
from payload_parser import parse, PAYLOAD_SIZE

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("sensegate.sim")

app = Flask(__name__)

# ── DB ────────────────────────────────────────────────────────────────────────

_db_conn = None


def _get_db():
    global _db_conn
    if _db_conn is None or _db_conn.closed:
        _db_conn = psycopg2.connect(os.environ["DB_URL"])
        _db_conn.autocommit = False
    return _db_conn


# ── Alert detection ───────────────────────────────────────────────────────────

def _detect_alerts(reading: dict) -> list:
    alerts = []
    if reading["state"] == 99:
        alerts.append({"type": "offline", "message": "Machine offline"})
    if reading["state"] == 31:
        alerts.append({"type": "alarm", "message": f"Machine alarm — alarm_seconds={reading['alarm_seconds']}"})
    if reading["alarm_seconds"] > 0 and reading["state"] not in (31, 99):
        alerts.append({"type": "alarm_time", "message": f"Accumulated alarm time {reading['alarm_seconds']}s"})
    return alerts


# ── DB helpers ────────────────────────────────────────────────────────────────

def _is_duplicate(conn, device_id: int, sequence: int) -> bool:
    with conn.cursor() as cur:
        cur.execute(
            "SELECT 1 FROM sequence_log WHERE device_id = %s AND sequence = %s",
            (device_id, sequence),
        )
        return cur.fetchone() is not None


def _insert_reading(conn, reading: dict, from_number: str = None):
    with conn.cursor() as cur:
        cur.execute(
            """
            INSERT INTO sensor_readings (
                time, device_id, sequence,
                state, state_name,
                pallet_id, wrap_time_s, wrap_transit_time_s,
                pallet_rotations, program_number, pallet_perimeter,
                running_seconds, alarm_seconds,
                machine_timestamp, from_number
            ) VALUES (
                NOW(), %(device_id)s, %(sequence)s,
                %(state)s, %(state_name)s,
                %(pallet_id)s, %(wrap_time_s)s, %(wrap_transit_time_s)s,
                %(pallet_rotations)s, %(program_number)s, %(pallet_perimeter)s,
                %(running_seconds)s, %(alarm_seconds)s,
                %(machine_timestamp)s, %(from_number)s
            )
            """,
            {**reading, "from_number": from_number},
        )
        cur.execute(
            "INSERT INTO sequence_log (device_id, sequence) VALUES (%s, %s) ON CONFLICT DO NOTHING",
            (reading["device_id"], reading["sequence"]),
        )


# ── /health ───────────────────────────────────────────────────────────────────

@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok"}), 200


# ── /ingest ───────────────────────────────────────────────────────────────────

@app.route("/ingest", methods=["POST"])
def ingest():
    body = request.get_json(force=True, silent=True) or {}
    hex_str   = body.get("hex", "").strip().upper()
    device_id = body.get("device_id")
    sequence  = body.get("sequence")
    # force=true skips dedup so simulation re-runs always store data
    force     = str(body.get("force", request.args.get("force", "0"))).lower() in ("1", "true", "yes")

    if not hex_str or device_id is None or sequence is None:
        return jsonify({"status": "error", "message": "hex, device_id, sequence required"}), 400

    if len(hex_str) != PAYLOAD_SIZE * 2:
        return jsonify({"status": "error", "message": f"hex must be {PAYLOAD_SIZE*2} chars"}), 400

    try:
        ct = bytes.fromhex(hex_str)
    except ValueError:
        return jsonify({"status": "error", "message": "invalid hex"}), 400

    try:
        pt = decrypt(ct, device_id, sequence)
        reading = parse(pt)
    except Exception as exc:
        logger.error("decrypt/parse error: %s", exc)
        return jsonify({"status": "error", "message": str(exc)}), 422

    alerts = _detect_alerts(reading)

    try:
        conn = _get_db()
        if force or not _is_duplicate(conn, reading["device_id"], reading["sequence"]):
            _insert_reading(conn, reading)
            conn.commit()
            stored = True
        else:
            stored = False
    except Exception as exc:
        logger.error("DB error: %s", exc)
        try:
            _get_db().rollback()
        except Exception:
            pass
        stored = False

    return jsonify({
        "status": "ok",
        "stored": stored,
        "fields": {
            "state":           reading["state"],
            "state_name":      reading["state_name"],
            "pallet_id":       reading["pallet_id"],
            "wrap_time":       reading["wrap_time_s"],
            "wrap_transit":    reading["wrap_transit_time_s"],
            "rotations":       reading["pallet_rotations"],
            "program_number":  reading["program_number"],
            "perimeter":       reading["pallet_perimeter"],
            "running_seconds": reading["running_seconds"],
            "alarm_seconds":   reading["alarm_seconds"],
            "sequence":        reading["sequence"],
            "device_id":       reading["device_id"],
        },
        "alerts": alerts,
    }), 200


# ── /sms (Twilio-style form POST, hex in Body field) ─────────────────────────

@app.route("/sms", methods=["POST"])
def sms():
    if request.content_type and "form" in request.content_type:
        form = request.form
    else:
        form = request.form or {}

    sms_body    = form.get("Body", "").strip().upper()
    from_number = form.get("From", "")

    # SMS carries SF_SLOT_SIZE*2 = 100 hex chars (current + previous packet)
    SF_HEX = PAYLOAD_SIZE * 2 * 2
    if len(sms_body) != SF_HEX:
        logger.error("SMS body wrong length: %d (expected %d)", len(sms_body), SF_HEX)
        return jsonify({"status": "error", "message": "bad length"}), 400

    try:
        raw = bytes.fromhex(sms_body)
    except ValueError:
        return jsonify({"status": "error", "message": "invalid hex"}), 400

    # Resolve device_id from MSISDN
    msisdn_map = {}
    try:
        msisdn_map = json.loads(os.environ.get("MSISDN_MAP", "{}"))
    except json.JSONDecodeError:
        pass
    normalized = from_number.lstrip("+").replace(" ", "")
    device_id = msisdn_map.get(normalized, 1)

    logger.info("SMS rx from=%s body=%s", from_number, sms_body)

    results = []
    for i in range(2):
        ct = raw[i * PAYLOAD_SIZE:(i + 1) * PAYLOAD_SIZE]
        # Fixed-point: decrypt with guess → read seq from plaintext → iterate
        # Converges in 1–2 steps under normal conditions
        found = None
        guess = 0
        last_exc = None
        visited = set()
        for _ in range(65536):
            if guess in visited:
                guess = (guess + 1) & 0xFFFF
                if guess in visited:
                    break
            visited.add(guess)
            try:
                pt = decrypt(ct, device_id, guess)
                reading = parse(pt)  # raises ValueError on CRC mismatch
                if reading["sequence"] == guess:
                    found = (guess, reading)
                    break
                guess = reading["sequence"]
            except ValueError as exc:
                last_exc = exc
                guess = (guess + 1) & 0xFFFF
            except Exception as exc:
                last_exc = exc
                break
        logger.info("  slot %d ct=%s found=%s err=%s", i, ct.hex().upper(),
                    (found[0] if found else None), last_exc)
        if found:
            seq, reading = found
            alerts = _detect_alerts(reading)
            stored = False
            try:
                conn = _get_db()
                if not _is_duplicate(conn, reading["device_id"], reading["sequence"]):
                    _insert_reading(conn, reading, from_number)
                    conn.commit()
                    stored = True
            except Exception as exc:
                logger.error("DB error: %s", exc)
                try:
                    _get_db().rollback()
                except Exception:
                    pass
            results.append({"slot": i, "sequence": seq, "state": reading["state"],
                            "alerts": alerts, "stored": stored})

    return jsonify({"status": "ok", "results": results}), 200


# ── /readings ─────────────────────────────────────────────────────────────────

@app.route("/readings", methods=["GET"])
def readings():
    limit = min(int(request.args.get("limit", 20)), 200)
    device_id = request.args.get("device_id")
    try:
        conn = _get_db()
        with conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as cur:
            if device_id:
                cur.execute(
                    "SELECT * FROM sensor_readings WHERE device_id=%s ORDER BY time DESC LIMIT %s",
                    (device_id, limit),
                )
            else:
                cur.execute(
                    "SELECT * FROM sensor_readings ORDER BY time DESC LIMIT %s",
                    (limit,),
                )
            rows = cur.fetchall()
    except Exception as exc:
        return jsonify({"status": "error", "message": str(exc)}), 500

    return jsonify({"status": "ok", "count": len(rows), "readings": [dict(r) for r in rows]}), 200


# ── /summary ─────────────────────────────────────────────────────────────────

@app.route("/summary", methods=["GET"])
def summary():
    try:
        conn = _get_db()
        with conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as cur:
            cur.execute("SELECT * FROM readings_hourly ORDER BY bucket DESC LIMIT 48")
            rows = cur.fetchall()
    except Exception as exc:
        return jsonify({"status": "error", "message": str(exc)}), 500

    return jsonify({"status": "ok", "count": len(rows), "summary": [dict(r) for r in rows]}), 200


# ── main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    port = int(os.environ.get("PORT", 8080))
    app.run(host="0.0.0.0", port=port, debug=False)
