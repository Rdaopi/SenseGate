"""
SenseGate — AES decrypt + unpack correctness test
Run: python test_decrypt.py
Exits 0 if all vectors pass, 1 if any fail.
"""

import os
import sys

os.environ["AES_KEY"] = "2B7E151628AED2A6ABF7158809CF4F3C"

# Add lambda dir to path so we can import from there
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "lambda"))

from aes_decrypt import decrypt
from payload_parser import parse

TEST_VECTORS = [
    {
        "device_id": 1,
        "sequence": 0,
        "encrypted_hex": "436E149A1C324E9D55A5AD34DDB5CB3E97E27025C748ACCD1A",
        "expected": {
            "state": 30,
            "pallet_id": 159302,
            "wrap_time_s": 668,
            "wrap_transit_time_s": 944,
            "pallet_rotations": 14,
            "program_number": 0,
            "pallet_perimeter": 3,
            "running_seconds": 4000,
            "alarm_seconds": 0,
        },
    },
    {
        "device_id": 1,
        "sequence": 9,
        "encrypted_hex": "C9C34588AEB97FC945671BEF6943FB099925C2DA4A26B3A285",
        "expected": {
            "state": 31,
            "pallet_id": 159202,
            "wrap_time_s": 19107,
            "wrap_transit_time_s": 21252,
            "pallet_rotations": 16,
            "program_number": 0,
            "pallet_perimeter": 3,
            "running_seconds": 4000,
            "alarm_seconds": 283,
        },
    },
    {
        "device_id": 1,
        "sequence": 12,
        "encrypted_hex": "03403C53F6BFFCAC092DA414C0D75AF651AC7646B64FD31E7F",
        "expected": {
            "state": 99,
            "pallet_id": 158308,
            "wrap_time_s": 0,
            "wrap_transit_time_s": 0,
            "pallet_rotations": 0,
            "program_number": 0,
            "pallet_perimeter": 3,
            "running_seconds": 4000,
            "alarm_seconds": 0,
        },
    },
]

STATE_LABELS = {30: "RUNNING", 31: "ALARM", 99: "OFFLINE"}

all_pass = True

for vec in TEST_VECTORS:
    seq = vec["sequence"]
    did = vec["device_id"]
    ct = bytes.fromhex(vec["encrypted_hex"])
    print(f"\n=== Vector seq={seq} device_id={did} ===")

    try:
        pt = decrypt(ct, did, seq)
    except Exception as exc:
        print(f"  FAIL decrypt: {exc}")
        all_pass = False
        continue

    try:
        result = parse(pt)
    except ValueError as exc:
        print(f"  FAIL parse: {exc}")
        all_pass = False
        continue

    for field, expected in vec["expected"].items():
        got = result.get(field)
        ok = got == expected
        status = "PASS" if ok else "FAIL"
        if not ok:
            all_pass = False
        print(f"  {status}  {field:24s}  expected={expected!r:>10}  got={got!r}")

print()
if all_pass:
    print("ALL VECTORS PASS")
    sys.exit(0)
else:
    print("SOME VECTORS FAILED")
    sys.exit(1)
