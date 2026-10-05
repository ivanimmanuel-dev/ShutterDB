"""Generate independently encoded CRC32C format fixtures for libFuzzer."""
from pathlib import Path
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: python tools/fuzz-seeds.py OUTPUT_DIRECTORY")
root = Path(sys.argv[1])


def crc(data):
    value = 0xffffffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x82f63b78 if value & 1 else 0)
    return value ^ 0xffffffff


header = b"SHUTDB\r\n" + struct.pack("<HHIQI", 1, 32, 0, 0, 0)
header += struct.pack("<I", crc(header))
records = []
for i in range(1, 5):
    payload = f"key{i}".encode() + b"value"
    record = b"SHDR" + struct.pack("<HBBQIIIII", 1, 1, 0, i, 4, 5, crc(payload), 49, 0)
    records.append(record + struct.pack("<I", crc(record)) + payload)
for target in ["record", "scanner", "recovery", "verify"]:
    directory = root / target
    directory.mkdir(parents=True, exist_ok=True)
    seed = records[0] if target == "record" else header + b"".join(records)
    (directory / "valid").write_bytes(seed)
    (directory / "tail").write_bytes(seed[:-3])
    (directory / "empty").write_bytes(header)
    # Seed truncation boundaries, tombstones and binary records.
    for size in range(len(seed)):
        (directory / f"cut-{size}").write_bytes(seed[:size])
    for kind, key, value in [(1, b"binary\0key", b"\0\xff"), (1, b"empty", b""), (2, b"key1", b"")]:
        payload = key + value
        record = b"SHDR" + struct.pack("<HBBQIIIII", 1, kind, 0, 5, len(key), len(value),
                                       crc(payload), 40 + len(payload), 0)
        record += struct.pack("<I", crc(record)) + payload
        (directory / f"kind-{kind}-{len(value)}").write_bytes(
            record if target == "record" else header + b"".join(records) + record)
