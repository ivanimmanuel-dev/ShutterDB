"""Run the persistence, corruption and compaction tour on disposable files."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("usage: python tools/demo.py PATH_TO_SHUTTER")
executable = str(Path(sys.argv[1]).resolve())

with tempfile.TemporaryDirectory(prefix="shutter-tour-") as directory:
    root = Path(directory)
    database = root / "app.shdb"

    def run(*args, code=0):
        display = [str(arg).replace(str(root) + "/", "").replace(str(root) + "\\", "") for arg in args]
        print("$ shutter " + " ".join(display), flush=True)
        result = subprocess.run([executable, *map(str, args)], capture_output=True, text=True, timeout=20)
        fallback = "Key absent (exit 1)" if result.returncode == 1 else "OK"
        print(result.stdout or result.stderr or fallback, end="" if (result.stdout or result.stderr).endswith("\n") else "\n", flush=True)
        if result.returncode != code:
            raise RuntimeError(f"expected exit {code}; got {result.returncode}")
        return result.stdout

    run("version")
    run("init", database)
    run("set", "username", "cosmos", "--db", database)
    run("set", "username", "cosmos-v2", "--db", database)
    run("set", "temporary", "discard-me", "--db", database)
    assert run("get", "username", "--db", database).strip() == "cosmos-v2"
    print("Each command above is a fresh process: the value survived reopening.\n", flush=True)
    run("delete", "temporary", "--db", database)
    run("get", "temporary", "--db", database, code=1)
    run("stats", "--db", database)
    copy = root / "corrupt-copy.shdb"
    shutil.copyfile(database, copy)
    corrupted = bytearray(copy.read_bytes())
    corrupted[32 + 40] ^= 1
    copy.write_bytes(corrupted)
    report = json.loads(run("verify", "--db", copy, "--json", code=4))
    assert report["offset"] == 32 and report["error"] == "CORRUPTION"
    assert copy.read_bytes() == corrupted
    run("verify", "--db", database)
    before = database.stat().st_size
    run("compact", "--db", database)
    assert database.stat().st_size < before
    assert run("get", "username", "--db", database).strip() == "cosmos-v2"
    run("get", "temporary", "--db", database, code=1)
    run("stats", "--db", database)
    run("verify", "--db", database)
    print("Tour passed. Original data survived restart and compaction; only a disposable copy was corrupted.")
