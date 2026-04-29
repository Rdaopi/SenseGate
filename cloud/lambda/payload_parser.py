"""
SenseGate — Payload Parser (v3 — TWIKO real fields, no reserved)
Bit-packing layout (25 bytes total = 23 data + 2 CRC):

  Bit   0–6   : state             (7  bits) — machine state code
  Bit   7–24  : pallet_id         (18 bits) — current pallet ID
  Bit  25–40  : wrap_time         (16 bits) — wrapping time in seconds
  Bit  41–56  : wrap_transit_time (16 bits) — wrapping + transit time in seconds
  Bit  57–68  : pallet_rotations  (12 bits) — rotations on pallet
  Bit  69–76  : program_number    (8  bits) — program number
  Bit  77–92  : pallet_perimeter  (16 bits) — pallet perimeter (unit TBD)
  Bit  93–109 : running_seconds   (17 bits) — daily accumulated running time
  Bit 110–126 : alarm_seconds     (17 bits) — daily accumulated alarm time
  Bit 127–158 : timestamp         (32 bits) — Unix epoch from machine clock
  Bit 159–174 : sequence          (16 bits) — packet counter
  Bit 175–182 : device_id         (8  bits) — node identifier
  Bit 183     : padding           (1  bit)  — zero
  Bytes 0–22  : data (184 bits = 23 bytes)  — CRC computed over these
  Bytes 23–24 : CRC16-CCITT big-endian

TOTAL: 25 bytes
Rolling redundancy (current + previous): 50 bytes → 100 hex chars via SMS
"""

PAYLOAD_SIZE = 25
DATA_SIZE    = 23
TOTAL_BITS   = DATA_SIZE * 8  # 184 bits

STATE_NAMES = {
    0:  "Power On",
    19: "Online",
    20: "Stop",
    30: "Running",
    31: "Waiting Materials",
    32: "Waiting Upstream",
    99: "Offline",
}


def _crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT (poly 0x1021, init 0xFFFF) — matches firmware crc16()."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


def _extract(bits_int: int, msb_pos: int, width: int) -> int:
    """Extract `width` bits starting at `msb_pos` from the left (bit 0 = MSB)."""
    shift = TOTAL_BITS - msb_pos - width
    return (bits_int >> shift) & ((1 << width) - 1)


def parse(raw: bytes) -> dict:
    """
    Parse a 25-byte decrypted payload.
    Returns a dict with physical values or raises ValueError on bad length or CRC.
    """
    if len(raw) != PAYLOAD_SIZE:
        raise ValueError(f"Expected {PAYLOAD_SIZE} bytes, got {len(raw)}")

    crc_received = (raw[23] << 8) | raw[24]
    crc_computed = _crc16_ccitt(raw[:DATA_SIZE])
    if crc_received != crc_computed:
        raise ValueError(
            f"CRC mismatch: received 0x{crc_received:04X}, computed 0x{crc_computed:04X}"
        )

    bits = int.from_bytes(raw[:DATA_SIZE], "big")

    state             = _extract(bits,   0,  7)
    pallet_id         = _extract(bits,   7, 18)
    wrap_time         = _extract(bits,  25, 16)
    wrap_transit_time = _extract(bits,  41, 16)
    pallet_rotations  = _extract(bits,  57, 12)
    program_number    = _extract(bits,  69,  8)
    pallet_perimeter  = _extract(bits,  77, 16)
    running_seconds   = _extract(bits,  93, 17)
    alarm_seconds     = _extract(bits, 110, 17)
    timestamp         = _extract(bits, 127, 32)
    sequence          = _extract(bits, 159, 16)
    device_id         = _extract(bits, 175,  8)

    return {
        "state":               state,
        "state_name":          STATE_NAMES.get(state, f"Unknown({state})"),
        "pallet_id":           pallet_id,
        "wrap_time_s":         wrap_time,
        "wrap_transit_time_s": wrap_transit_time,
        "pallet_rotations":    pallet_rotations,
        "program_number":      program_number,
        "pallet_perimeter":    pallet_perimeter,
        "running_seconds":     running_seconds,
        "alarm_seconds":       alarm_seconds,
        "machine_timestamp":   timestamp,
        "sequence":            sequence,
        "device_id":           device_id,
    }


def pack(
    state: int,
    pallet_id: int,
    wrap_time: int,
    wrap_transit_time: int,
    pallet_rotations: int,
    program_number: int,
    pallet_perimeter: int,
    running_seconds: int,
    alarm_seconds: int,
    timestamp: int,
    sequence: int,
    device_id: int,
) -> bytes:
    """Build a 25-byte payload — mirrors the C firmware pack_payload()."""
    bits = 0

    def _pack(val: int, msb_pos: int, width: int):
        nonlocal bits
        shift = TOTAL_BITS - msb_pos - width
        bits |= (val & ((1 << width) - 1)) << shift

    _pack(state,              0,  7)
    _pack(pallet_id,          7, 18)
    _pack(wrap_time,         25, 16)
    _pack(wrap_transit_time, 41, 16)
    _pack(pallet_rotations,  57, 12)
    _pack(program_number,    69,  8)
    _pack(pallet_perimeter,  77, 16)
    _pack(running_seconds,   93, 17)
    _pack(alarm_seconds,    110, 17)
    _pack(timestamp,        127, 32)
    _pack(sequence,         159, 16)
    _pack(device_id,        175,  8)

    data = bits.to_bytes(DATA_SIZE, "big")
    crc  = _crc16_ccitt(data)
    return data + bytes([crc >> 8, crc & 0xFF])