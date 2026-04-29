"""
SenseGate — Webhook Handler (v2 — TWIKO real fields)
Receives Twilio POST for incoming NB-IoT binary SMS,
decrypts and parses the 56-byte rolling buffer (current + previous packet),
deduplicates by sequence number, and writes to Supabase (PostgreSQL).

Environment variables:
  AES_KEY           — 32 hex chars (16-byte AES key)
  DB_URL            — PostgreSQL DSN (Supabase connection string)
  TWILIO_AUTH_TOKEN — Twilio webhook signature validation
  MSISDN_MAP        — JSON map of MSISDN to device_id
                      e.g. '{"393001234567": 1, "393007654321": 2}'
"""

import hashlib
import hmac
import json
import logging
import os
import urllib.parse

import psycopg2

from aes_decrypt import decrypt
from payload_parser import parse, _crc16_ccitt, PAYLOAD_SIZE

logger = logging.getLogger()
logger.setLevel(logging.INFO)

# Reused across warm invocations
_db_conn = None

def _get_db():
    global _db_conn
    if _db_conn is None or _db_conn.closed:
        _db_conn = psycopg2.connect(os.environ["DB_URL"])
    return _db_conn


# ── MSISDN → device_id ────────────────────────────────────────────────────────
def _msisdn_to_device_id(from_number: str):
    """
    Look up device_id from the MSISDN_MAP env var (JSON).
    Normalizes the number by stripping '+' and spaces.
    Returns None if the sender is unknown.
    """
    raw_map = os.environ.get("MSISDN_MAP", "{}")
    try:
        msisdn_map = json.loads(raw_map)
    except json.JSONDecodeError:
        logger.error("MSISDN_MAP is not valid JSON")
        return None
    normalized = from_number.lstrip("+").replace(" ", "")
    return msisdn_map.get(normalized)


# ── Fixed-point iteration to recover sequence number ─────────────────────────
def _extract_seq_from_plaintext(pt: bytes) -> int:
    """Extract sequence from bits 167-182 of the 28-byte payload."""
    bits = int.from_bytes(pt[:26], "big")
    return (bits >> (208 - 167 - 16)) & 0xFFFF


def _find_sequence(ciphertext_56: bytes, device_id: int, last_seq: int):
    """
    Recover sequence number using fixed-point iteration.

    The AES-CTR nonce depends on seq, and the plaintext contains seq
    at a known bit position — this forms a fixed-point equation:

        f(x) = decrypt with nonce(x) → extract seq from plaintext
        if f(x) == x: found

    Converges in 1 iteration under normal conditions.
    Falls back to linear scan if fixed-point does not converge (rare).

    Returns (seq, plaintext_28) or None.
    """
    MAX_FP_ITER = 16
    current_ct  = ciphertext_56[:PAYLOAD_SIZE]
    guess = (last_seq + 1) % 65536

    for i in range(MAX_FP_ITER):
        pt       = decrypt(current_ct, device_id, guess)
        next_seq = _extract_seq_from_plaintext(pt)
        if next_seq == guess:
            # Verify CRC to rule out accidental collision
            crc_ok = _crc16_ccitt(pt[:26]) == (pt[26] << 8) | pt[27]
            if crc_ok:
                logger.info("Fixed-point converged in %d iter: seq=%d", i + 1, guess)
                return guess, pt
            break   # CRC failed — collision, fall through to linear scan
        guess = next_seq

    # Linear fallback — rare: first boot, large gap, or hash collision
    logger.warning("Fixed-point did not converge — falling back to linear scan")
    start = (last_seq + 1) % 65536
    for i in range(65536):
        seq = (start + i) % 65536
        pt  = decrypt(current_ct, device_id, seq)
        if _crc16_ccitt(pt[:26]) == (pt[26] << 8) | pt[27]:
            logger.info("Linear fallback: seq=%d found in %d attempts", seq, i + 1)
            return seq, pt

    return None


# ── Twilio webhook signature validation ───────────────────────────────────────
def _validate_twilio_signature(event: dict) -> bool:
    """
    Validates the X-Twilio-Signature header to ensure the request
    genuinely originates from Twilio.
    """
    auth_token = os.environ.get("TWILIO_AUTH_TOKEN", "")
    if not auth_token:
        logger.warning("TWILIO_AUTH_TOKEN not set — skipping signature check (dev mode)")
        return True

    headers   = event.get("headers", {})
    signature = headers.get("X-Twilio-Signature") or headers.get("x-twilio-signature", "")
    domain    = headers.get("Host") or headers.get("host", "")
    path      = event.get("rawPath", event.get("path", "/"))
    url       = f"https://{domain}{path}"

    body = event.get("body", "")
    if event.get("isBase64Encoded"):
        import base64
        body = base64.b64decode(body).decode("utf-8")

    params = sorted(urllib.parse.parse_qsl(body, keep_blank_values=True))
    url_with_params = url + "".join(k + v for k, v in params)

    import base64 as _b64
    expected = _b64.b64encode(
        hmac.new(auth_token.encode(), url_with_params.encode(), hashlib.sha1).digest()
    ).decode()
    return hmac.compare_digest(signature, expected)


def _parse_twilio_body(event: dict) -> dict:
    body = event.get("body", "")
    if event.get("isBase64Encoded"):
        import base64
        body = base64.b64decode(body).decode("utf-8")
    return dict(urllib.parse.parse_qsl(body, keep_blank_values=True))


# ── Sequence deduplication ────────────────────────────────────────────────────
def _is_duplicate(conn, device_id: int, sequence: int) -> bool:
    with conn.cursor() as cur:
        cur.execute(
            "SELECT 1 FROM sequence_log WHERE device_id = %s AND sequence = %s",
            (device_id, sequence),
        )
        return cur.fetchone() is not None


def _record_sequence(conn, device_id: int, sequence: int):
    with conn.cursor() as cur:
        cur.execute(
            "INSERT INTO sequence_log (device_id, sequence) VALUES (%s, %s) ON CONFLICT DO NOTHING",
            (device_id, sequence),
        )


def _get_last_seq(conn, device_id: int) -> int:
    """Returns the last known sequence for this device, or -1 if none."""
    with conn.cursor() as cur:
        cur.execute(
            "SELECT MAX(sequence) FROM sequence_log WHERE device_id = %s",
            (device_id,),
        )
        row = cur.fetchone()
        return row[0] if row[0] is not None else -1


# ── Database write ─────────────────────────────────────────────────────────────
def _insert_reading(conn, reading: dict, from_number: str):
    with conn.cursor() as cur:
        cur.execute(
            """
            INSERT INTO sensor_readings (
                time, device_id, sequence,
                state, state_name,
                pallet_id, wrap_time_s, wrap_transit_time_s,
                running_seconds, alarm_seconds,
                machine_timestamp, from_number
            ) VALUES (
                NOW(), %(device_id)s, %(sequence)s,
                %(state)s, %(state_name)s,
                %(pallet_id)s, %(wrap_time_s)s, %(wrap_transit_time_s)s,
                %(running_seconds)s, %(alarm_seconds)s,
                %(machine_timestamp)s, %(from_number)s
            )
            """,
            {**reading, "from_number": from_number},
        )


# ── Main handler ──────────────────────────────────────────────────────────────
def handler(event: dict, context) -> dict:

    # 1. Validate Twilio signature
    if not _validate_twilio_signature(event):
        return {"statusCode": 403, "body": "Forbidden"}

    # 2. Parse webhook fields
    twilio      = _parse_twilio_body(event)
    sms_body    = twilio.get("Body", "").strip()
    from_number = twilio.get("From", "")
    logger.info("SMS from %s | %d chars", from_number, len(sms_body))

    # 3. Resolve device_id from sender MSISDN
    device_id = _msisdn_to_device_id(from_number)
    if device_id is None:
        logger.error("Unknown MSISDN: %s", from_number)
        return _twiml_ok()

    # 4. Decode hex → 56 bytes (2 × 28-byte encrypted packets)
    expected_hex = PAYLOAD_SIZE * 2 * 2  # 112 hex chars
    if len(sms_body) != expected_hex:
        logger.error("Unexpected SMS length: %d chars (expected %d)", len(sms_body), expected_hex)
        return _twiml_ok()

    try:
        ciphertext_56 = bytes.fromhex(sms_body)
    except ValueError:
        logger.error("SMS body is not valid hex")
        return _twiml_ok()

    # 5. Get last known sequence from DB to seed the fixed-point search
    try:
        conn     = _get_db()
        last_seq = _get_last_seq(conn, device_id)
    except Exception as exc:
        logger.error("DB error (get_last_seq): %s", exc)
        conn     = None
        last_seq = -1

    # 6. Recover sequence number via fixed-point iteration
    result = _find_sequence(ciphertext_56, device_id, last_seq)
    if result is None:
        logger.error("Could not find valid sequence — corrupted packet or wrong key")
        return _twiml_ok()

    seq, current_pt = result
    prev_seq = (seq - 1) % 65536
    readings = []

    # 7. Parse current packet (bytes 0–27)
    try:
        readings.append(("current", parse(current_pt)))
    except ValueError as exc:
        logger.error("[current] parse error: %s", exc)

    # 8. Decrypt and parse previous packet (bytes 28–55, nonce uses seq-1)
    try:
        prev_pt = decrypt(ciphertext_56[PAYLOAD_SIZE:], device_id, prev_seq)
        readings.append(("previous", parse(prev_pt)))
    except ValueError as exc:
        logger.warning("[previous] parse/CRC error: %s", exc)

    if not readings:
        return _twiml_ok()

    # 9. Write to DB with deduplication
    if conn:
        try:
            for label, reading in readings:
                s, d = reading["sequence"], reading["device_id"]
                if _is_duplicate(conn, d, s):
                    logger.info("[%s] duplicate seq=%d — skipped", label, s)
                    continue
                _insert_reading(conn, reading, from_number)
                _record_sequence(conn, d, s)
                logger.info("[%s] inserted seq=%d state=%s", label, s, reading["state_name"])
            conn.commit()
        except Exception as exc:
            logger.error("DB error (insert): %s", exc)
            conn.rollback()

    return _twiml_ok()


def _twiml_ok() -> dict:
    """Return 200 with empty TwiML — tells Twilio not to send an SMS reply."""
    return {
        "statusCode": 200,
        "headers": {"Content-Type": "text/xml"},
        "body": '<?xml version="1.0" encoding="UTF-8"?><Response></Response>',
    }