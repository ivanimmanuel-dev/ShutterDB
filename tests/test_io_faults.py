"""Exercise real syscall return paths using a test-only interposer on Linux."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, interposer = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
cases = 0
with tempfile.TemporaryDirectory(prefix="shutter-io-faults-") as folder:
    root = Path(folder)

    def run(db, *args, code=0, fault=None):
        env = os.environ.copy()
        if fault:
            operation, ordinal, mode = fault
            env.update(LD_PRELOAD=interposer, SHUTTER_IO_OPERATION=operation,
                       SHUTTER_IO_CALL=str(ordinal), SHUTTER_IO_MODE=mode)
        command = [exe, *args, "--db", str(db)]
        p = subprocess.run(command, capture_output=True, env=env, timeout=10)
        assert p.returncode == code, (command, fault, p.returncode, p.stdout, p.stderr)
        return p.stdout

    def fresh():
        global cases
        cases += 1
        db = root / f"case-{cases}.shdb"
        run(db, "init", str(db))
        run(db, "set", "acknowledged", "must-survive")
        run(db, "set", "deleted", "old")
        run(db, "delete", "deleted")
        return db

    def check(db):
        assert run(db, "get", "acknowledged").strip() == b"must-survive"
        run(db, "get", "deleted", code=1)
        assert json.loads(run(db, "verify", "--json"))["ok"]

    for operation in ["pwrite", "pread"]:
        for mode in ["short", "eintr"]:
            db = fresh()
            run(db, "set", "new", "value", fault=(operation, 1, mode))
            assert run(db, "get", "new").strip() == b"value"
            check(db)
    db = fresh()
    run(db, "set", "new", "value", fault=("fsync", 1, "eintr"))
    check(db)
    for ordinal in [1, 2]:
        db = fresh()
        before = db.read_bytes()
        run(db, "set", "new", "value", code=3, fault=("pwrite", ordinal, "enospc"))
        if ordinal == 2:
            report = json.loads(run(db, "verify", "--json", code=4))
            assert report["truncated_tail"]
            partial = db.read_bytes()
            run(db, "recover", code=3, fault=("ftruncate", 1, "eio"))
            assert db.read_bytes() == partial
            run(db, "recover")
        assert db.read_bytes() == before
        check(db)
    db = fresh()
    run(db, "set", "uncertain", "may-exist", code=3, fault=("fsync", 1, "eio"))
    check(db)
    assert run(db, "get", "uncertain").strip() == b"may-exist"
    for operation, ordinals in [("rename", [1, 2]), ("fsync", range(1, 7)), ("pwrite", [1, 2, 3])]:
        for ordinal in ordinals:
            db = fresh()
            before = db.read_bytes()
            run(db, "compact", code=3, fault=(operation, ordinal, "eio"))
            if operation == "rename":
                assert db.read_bytes() == before
            check(db)
            run(db, "compact")
            check(db)
    db = fresh()
    before = db.read_bytes()
    run(db, "get", "acknowledged", code=3, fault=("pread", 1, "eio"))
    assert db.read_bytes() == before
    check(db)
    for operation, ordinal in [("pwrite", 1), ("fsync", 1), ("rename", 1), ("fsync", 2)]:
        cases += 1
        db = root / f"init-{cases}.shdb"
        run(db, "init", str(db), code=3, fault=(operation, ordinal, "eio"))
        # A failed publication may leave no primary or a valid primary, never a partial one.
        if db.exists():
            run(db, "verify")
        run(db, "init", str(db))
        run(db, "set", "acknowledged", "must-survive")
        check(db)
print(f"{cases} syscall fault scenarios passed: short I/O, EINTR, ENOSPC, read/sync/truncate/rename errors")
