"""Measure bounded-cache writes, eviction pauses, reads and reopening in fresh processes."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--directory", type=Path)
parser.add_argument("--writes", type=int, default=4096)
parser.add_argument("--sizes", type=int, nargs="+", default=[4096, 65536])
parser.add_argument("--max-mib", type=int, default=64)
parser.add_argument("--sync-every", type=int, default=128)
parser.add_argument("--repeats", type=int, default=3)
args = parser.parse_args()
if args.repeats < 1:
    parser.error("--repeats must be positive")
repo = Path(__file__).resolve().parents[1]
executable = args.executable.resolve()
source_hashes = {
    path.relative_to(repo).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
    for directory in ("src", "include") for path in sorted((repo / directory).rglob("*")) if path.is_file()
}
report = {
    "date": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "system": platform.platform(), "writes": args.writes, "sizes": args.sizes,
    "max_mib": args.max_mib, "sync_every": args.sync_every, "repeats": args.repeats,
    "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
    "benchmark_sha256": hashlib.sha256((repo / "benchmarks/cache.cpp").read_bytes()).hexdigest(),
    "source_sha256": source_hashes, "complete": False, "results": [],
}
args.output.parent.mkdir(parents=True, exist_ok=True)
for repeat in range(1, args.repeats + 1):
    sizes = args.sizes if repeat % 2 else list(reversed(args.sizes))
    for size in sizes:
        command = [str(executable), "--writes", str(args.writes), "--value-size", str(size),
                   "--max-mib", str(args.max_mib), "--sync-every", str(args.sync_every)]
        if args.directory:
            command.extend(["--directory", str(args.directory)])
        result = json.loads(subprocess.check_output(command, timeout=1800))
        if not result["verified"] or result["database_bytes"] > result["max_bytes"]:
            raise RuntimeError("cache benchmark verification failed")
        result["repeat"] = repeat
        report["results"].append(result)
        report["complete"] = len(report["results"]) == args.repeats * len(args.sizes)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{size} bytes, repeat {repeat}: {result['evictions']} evictions; "
              f"maximum write {result['insert']['max_ms']:.2f} ms", flush=True)
