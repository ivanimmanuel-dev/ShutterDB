"""Run a million operations across process restarts and compare against regenerated values."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--directory", type=Path, default=None)
args = parser.parse_args()
executable = str(args.executable.resolve())
results = []
started = time.monotonic()
with tempfile.TemporaryDirectory(prefix="shutter-stress-", dir=args.directory) as folder:
    root = Path(folder)
    database = root / "stress.shdb"
    for phase, batch in [("mutate", n) for n in range(10)] + [("compact", 0), ("check", 0), ("memory", 0)]:
        path = root / "memory.shdb" if phase == "memory" else database
        command = [executable, phase, str(path), "100000", "1000000", str(batch), "10"]
        result = json.loads(subprocess.check_output(command, timeout=300))
        results.append(result)
        print(f"{phase} {batch}: passed", flush=True)
assert sum(r.get("operations", 0) for r in results) == 1000000
report = {"passed": True, "seed": 20261005, "operations": 1000000, "distinct_keys": 100000,
          "independent_processes": len(results), "seconds": time.monotonic() - started,
          "durability": "buffered with explicit sync every 10000 operations and at each batch end",
          "oracle": "key-indexed generation and presence only; expected binary values regenerated on demand",
          "results": results}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(f"Passed 1,000,000 operations over 100,000 distinct keys; results: {args.output}")
