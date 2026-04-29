"""
SenseGate — AES-128-CTR decryption
Mirrors the C implementation in aes_ctr.c.

Key:    16 bytes, from environment variable (hex string)
Nonce:  8 bytes  = device_id (1B) + sequence (2B) + zero-padded to 8B
Counter starts at 0, increments per 16-byte block.
"""

import os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend


def _build_nonce(device_id: int, sequence: int) -> bytes:
    """
    Reconstruct the 16-byte CTR counter block used by the firmware.
    Layout: [device_id 1B][sequence 2B big-endian][zeros 13B]
    Counter field within the block starts at 0.
    """
    return bytes([device_id]) + sequence.to_bytes(2, "big") + bytes(13)


def decrypt(ciphertext: bytes, device_id: int, sequence: int) -> bytes:
    """
    Decrypt `ciphertext` with AES-128-CTR.
    Key is read from the AES_KEY env var (32 hex chars = 16 bytes).
    Raises ValueError if the key is missing or malformed.
    """
    key_hex = os.environ.get("AES_KEY", "")
    if len(key_hex) != 32:
        raise ValueError("AES_KEY env var must be 32 hex chars (16 bytes)")

    key = bytes.fromhex(key_hex)
    nonce = _build_nonce(device_id, sequence)

    cipher = Cipher(
        algorithms.AES(key),
        modes.CTR(nonce),
        backend=default_backend(),
    )
    decryptor = cipher.decryptor()
    return decryptor.update(ciphertext) + decryptor.finalize()