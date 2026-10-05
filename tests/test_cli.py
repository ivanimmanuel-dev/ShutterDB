"""Black-box process tests. Every database lives in a temporary directory."""
import json
import pathlib
import subprocess
import sys
import tempfile

exe = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="shutter-cli-") as folder:
    root = pathlib.Path(folder)
    db = root / "app.shdb"

    def run(*args, code=0):
        p = subprocess.run([exe, *map(str, args)], capture_output=True, timeout=20)
        assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
        return p.stdout

    run("version")
    run("--help")
    run("set", "x", "y", "--db", db, code=3)
    assert not db.exists()
    run("init", db)
    run("set", "username", "cosmos", "--db", db)
    assert run("get", "username", "--db", db).strip() == b"cosmos"
    assert json.loads(run("get", "username", "--db", db, "--json"))["value"] == b"cosmos".hex()
    value = root / "value.bin"
    value.write_bytes(bytes(range(256)) + b"\0\r\n")
    run("set", "binary", "--value-file", value, "--db", db)
    assert run("get", "binary", "--raw", "--db", db) == value.read_bytes()
    run("delete", "username", "--db", db)
    run("get", "username", "--db", db, code=1)
    run("delete", "username", "--db", db, code=1)
    assert json.loads(run("stats", "--db", db, "--json"))["tombstones"] == 1
    before = db.stat().st_size
    run("compact", "--db", db)
    assert db.stat().st_size < before
    assert json.loads(run("verify", "--db", db, "--json"))["ok"]
    assert run("get", "binary", "--raw", "--db", db) == value.read_bytes()
    broken = root / "corrupt.shdb"
    data = bytearray(db.read_bytes())
    data[-1] ^= 1
    broken.write_bytes(data)
    report = json.loads(run("verify", "--db", broken, "--json", code=4))
    assert report["error"] == "CORRUPTION" and report["offset"] == 32
    assert broken.read_bytes() == data
    partial = root / "partial.shdb"
    partial.write_bytes(db.read_bytes()[:-1])
    original = partial.read_bytes()
    assert json.loads(run("verify", "--db", partial, "--json", code=4))["truncated_tail"]
    assert partial.read_bytes() == original
    run("get", "binary", "--db", partial, code=4)
    run("recover", "--db", partial)
    run("verify", "--db", partial)
    run("set", "--unknown", "--db", db, code=2)
    run("set", "--db", db, "--", "--key", "--value")
    assert run("get", "--db", db, "--", "--key").strip() == b"--value"
print("CLI persistence, binary round trip, tombstones, compaction, corruption and recovery passed")
