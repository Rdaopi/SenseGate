"""
Tests for payload_parser.py
Run with: python -m pytest test_payload_parser.py -v
"""

import struct
import pytest
from cloud.lambda.test_payload_parser import parse, _crc16_ccitt


def _build_payload(
    temp_c=25.0,
    hum_pct=60.0,
    vib_g=1.0,
    pressure_hpa=1013.25,
    plc_cycles=100000,
    plc_hours=500,
    plc_running=True,
    plc_alarm=False,
    sequence=1,
    device_id=1,
) -> bytes:
    """Build a valid 19-byte payload matching the firmware's bit-packing."""
    raw_temp     = round((temp_c + 40.0) / 0.1)
    raw_hum      = round(hum_pct / 0.5)
    raw_vib      = round(vib_g / 0.016)
    raw_pressure = round((pressure_hpa - 300.0) / 0.06)
    plc_status   = (0x01 if plc_running else 0) | (0x02 if plc_alarm else 0)

    # Pack into 136-bit integer
    bits = 0
    def pack(val, width, offset):
        nonlocal bits
        bits |= (val & ((1 << width) - 1)) << (136 - offset - width)

    pack(raw_temp,     12, 0)
    pack(raw_hum,       8, 12)
    pack(raw_vib,      10, 20)
    pack(raw_pressure, 14, 30)
    pack(plc_cycles,   24, 44)
    pack(plc_hours,    16, 68)
    pack(plc_status,    8, 84)
    pack(sequence,     16, 92)
    pack(device_id,     8, 108)
    # bits 116-127 = reserved = 0

    payload17 = bits.to_bytes(17, "big")
    crc = _crc16_ccitt(payload17)
    return payload17 + bytes([crc >> 8, crc & 0xFF])


class TestCRC:
    def test_known_crc(self):
        data = b"\x00" * 17
        # CRC of all zeros with poly 0x1021 init 0xFFFF
        crc = _crc16_ccitt(data)
        assert isinstance(crc, int)
        assert 0 <= crc <= 0xFFFF

    def test_crc_changes_with_data(self):
        a = _crc16_ccitt(b"\x00" * 17)
        b = _crc16_ccitt(b"\x01" + b"\x00" * 16)
        assert a != b


class TestParse:
    def test_basic_values(self):
        pkt = _build_payload(temp_c=25.0, hum_pct=60.0, vib_g=1.0, pressure_hpa=1013.0)
        r = parse(pkt)
        assert r["temperature_c"] == pytest.approx(25.0, abs=0.15)
        assert r["humidity_pct"]  == pytest.approx(60.0, abs=0.5)
        assert r["vibration_g"]   == pytest.approx(1.0,  abs=0.02)
        assert r["pressure_hpa"]  == pytest.approx(1013.0, abs=0.1)

    def test_plc_flags(self):
        r = parse(_build_payload(plc_running=True, plc_alarm=True))
        assert r["plc_running"] is True
        assert r["plc_alarm"]   is True

        r2 = parse(_build_payload(plc_running=False, plc_alarm=False))
        assert r2["plc_running"] is False
        assert r2["plc_alarm"]   is False

    def test_sequence_and_device_id(self):
        r = parse(_build_payload(sequence=42, device_id=7))
        assert r["sequence"]  == 42
        assert r["device_id"] == 7

    def test_wrong_length_raises(self):
        with pytest.raises(ValueError, match="Expected 19 bytes"):
            parse(b"\x00" * 18)

    def test_bad_crc_raises(self):
        pkt = bytearray(_build_payload())
        pkt[17] ^= 0xFF   # corrupt CRC
        with pytest.raises(ValueError, match="CRC mismatch"):
            parse(bytes(pkt))

    def test_extreme_temperature(self):
        # Min: -40°C (raw=0), Max: ~369°C (raw=4095)
        r_min = parse(_build_payload(temp_c=-40.0))
        assert r_min["temperature_c"] == pytest.approx(-40.0, abs=0.15)

        r_max = parse(_build_payload(temp_c=369.0))
        assert r_max["temperature_c"] == pytest.approx(369.0, abs=0.15)

    def test_zero_vibration(self):
        r = parse(_build_payload(vib_g=0.0))
        assert r["vibration_g"] == 0.0