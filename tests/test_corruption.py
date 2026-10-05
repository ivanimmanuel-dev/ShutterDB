"""Check CLI diagnostics and recovery for damaged files."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=Path)
parser.add_argument("--report", type=Path)
args = parser.parse_args()
exe = str(args.executable.resolve())


def crc(data):
    value = 0xffffffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x82f63b78 if value & 1 else 0)
    return value ^ 0xffffffff


def run(db, *arguments, code=0):
    result = subprocess.run([exe, *arguments, "--db", str(db)], capture_output=True, timeout=10)
    assert result.returncode == code, (arguments, result.returncode, result.stdout, result.stderr)
    return result


reports = []
with tempfile.TemporaryDirectory(prefix="shutter-corruption-") as directory:
    root = Path(directory)
    valid = root / "original.shdb"
    run(valid, "init", str(valid))
    for key, value in [("first", "acknowledged"), ("middle", "payload"), ("last", "final")]:
        run(valid, "set", key, value)
    original = valid.read_bytes()
    offsets = [32]
    for _ in range(2):
        offsets.append(offsets[-1] + struct.unpack_from("<I", original, offsets[-1] + 28)[0])
    middle, last = offsets[1:]
    cases = []

    def flip(name, offset, diagnostic_offset):
        data = bytearray(original)
        data[offset] ^= 0x80
        cases.append((name, data, diagnostic_offset, False, "CORRUPTION"))

    def field(name, start, offset, fmt, value, diagnostic_offset=None, error="CORRUPTION"):
        data = bytearray(original)
        struct.pack_into(fmt, data, start + offset, value)
        length = 28 if start == 0 else 36
        struct.pack_into("<I", data, start + length, crc(data[start:start + length]))
        cases.append((name, data, start if diagnostic_offset is None else diagnostic_offset, False, error))

    flip("file-magic", 0, 0)
    flip("file-checksum", 28, 0)
    flip("record-magic", middle, middle)
    flip("record-header", middle + 12, middle)
    flip("record-header-checksum", middle + 36, middle)
    flip("key-bytes", middle + 40, middle)
    flip("value-bytes", middle + 46, middle)
    flip("last-payload", len(original) - 1, last)
    field("payload-checksum", middle, 24, "<I", 0)
    field("zero-key-length", middle, 16, "<I", 0)
    field("huge-key-length", middle, 16, "<I", 0xffffffff)
    field("huge-value-length", middle, 20, "<I", 0xffffffff)
    field("wrong-total-length", middle, 28, "<I", 0xffffffff)
    field("zero-sequence", middle, 8, "<Q", 0)
    field("duplicate-sequence", middle, 8, "<Q", 1)
    field("decreasing-sequence", last, 8, "<Q", 1)
    field("record-type", middle, 6, "<B", 255)
    field("delete-with-value", middle, 6, "<B", 2)
    field("file-reserved", 0, 24, "<I", 1)
    field("file-version", 0, 8, "<H", 2, 8, "UNSUPPORTED_FORMAT")
    field("record-version", middle, 4, "<H", 2, error="UNSUPPORTED_FORMAT")
    cases.extend([
        ("empty-file", b"", 0, False, "CORRUPTION"),
        ("zero-garbage", bytes(512), 0, False, "CORRUPTION"),
        ("random-garbage", random.Random(20261005).randbytes(512), 0, False, "CORRUPTION"),
        ("truncated-file-header", original[:31], 0, False, "CORRUPTION"),
        ("truncated-record-header", original[:last + 19], last, True, "CORRUPTION"),
        ("truncated-record-payload", original[:-1], last, True, "CORRUPTION"),
        ("trailing-garbage", original + b"junk", len(original), False, "CORRUPTION"),
    ])
    for name, data, offset, tail, category in cases:
        db = root / f"{name}.shdb"
        db.write_bytes(data)
        report = json.loads(run(db, "verify", "--json", code=4).stdout)
        assert not report["ok"] and report["offset"] == offset, (name, report)
        assert report["error"] == category and report["truncated_tail"] == tail, (name, report)
        opened = run(db, "get", "first", code=4)
        assert opened.stderr, name
        assert db.read_bytes() == data, name
        if tail:
            run(db, "recover")
            assert db.read_bytes() == original[:last]
            assert run(db, "get", "first").stdout.strip() == b"acknowledged"
            run(db, "get", "last", code=1)
            run(db, "verify")
        else:
            run(db, "recover", code=4)
            assert db.read_bytes() == data, name
        reports.append({"case": name, "verify": report, "open_stderr": opened.stderr.decode().strip()})
    assert valid.read_bytes() == original
if args.report:
    args.report.write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
print(f"{len(reports)} CLI corruption cases passed")
