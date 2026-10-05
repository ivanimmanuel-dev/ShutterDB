"""Compare asset-cache workloads in fresh processes, alternating engine order."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('--directory', type=Path)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--repeats', type=int, default=3)
parser.add_argument('--count', type=int, default=2048)
parser.add_argument('--sizes', type=int, nargs='+', default=[4096, 16384, 65536])
parser.add_argument('--modes', nargs='+', choices=['sync', 'batch'], default=['sync', 'batch'])
parser.add_argument('--engines', nargs='+', choices=['shutter', 'sqlite', 'sqlite-tuned', 'rocksdb'],
                    default=['shutter', 'sqlite', 'sqlite-tuned'])
parser.add_argument('--rounds', type=int, default=2, help='Update/delete rounds; zero measures initial ingestion and reads only')
parser.add_argument('--evict-cache', action='store_true', help='Request Linux file-cache eviction before each phase')
parser.add_argument('--engine-ref', help='Git revision of the engine compiled into the executable')
args = parser.parse_args()
if args.repeats < 1 or args.count < 4 or args.count > 100000 or args.count % 4:
    parser.error('positive repeats and a count divisible by four (4..100000) are required')
if not 0 <= args.rounds <= 255 or any(size < 16 or size > 16777216 for size in args.sizes):
    parser.error('rounds must be 0..255 and values 16..16777216 bytes')
if args.evict_cache and not hasattr(os, 'posix_fadvise'):
    parser.error('--evict-cache requires posix_fadvise')
executable = str(args.executable.resolve())
repo = Path(__file__).resolve().parents[1]
engine_ref = None
if args.engine_ref:
    engine_ref = subprocess.check_output(['git', 'rev-parse', args.engine_ref + '^{commit}'], cwd=repo).decode().strip()
paths = (subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', engine_ref, '--', 'src', 'include'],
                                cwd=repo).decode().splitlines() if engine_ref else
         [path.relative_to(repo).as_posix() for directory in ['src', 'include']
          for path in sorted((repo / directory).rglob('*')) if path.is_file()])
source_hashes = {}
for relative in paths:
    contents = (subprocess.check_output(['git', 'show', engine_ref + ':' + relative], cwd=repo)
                if engine_ref else (repo / relative).read_bytes())
    source_hashes[relative] = hashlib.sha256(contents).hexdigest()
phases = [('populate', 0), ('read', 0)]
for round_number in range(1, args.rounds + 1):
    phases.extend([('churn', round_number), ('reopen', round_number)])
if args.rounds:
    phases.extend([('compact', args.rounds), ('compacted-read', args.rounds)])
metadata = {
    'workload_version': 2, 'rounds': args.rounds,
    'cache_state': 'eviction-requested' if args.evict_cache else 'warm',
    'system': platform.platform(), 'seed': 20261005, 'count': args.count,
    'repeats': args.repeats, 'sizes': args.sizes, 'modes': args.modes, 'engines': args.engines,
    'executable_sha256': hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
    'engine_ref': engine_ref, 'source_sha256': source_hashes,
    'benchmark_sha256': hashlib.sha256((repo / 'benchmarks/asset_cache.cpp').read_bytes()).hexdigest(),
}
expected = len(args.sizes) * len(args.modes) * args.repeats * len(args.engines) * len(phases)
results = []
for size in args.sizes:
    for mode in args.modes:
        for repeat in range(1, args.repeats + 1):
            engines = args.engines.copy()
            if repeat % 2 == 0:
                engines.reverse()
            for engine in engines:
                with tempfile.TemporaryDirectory(prefix='shutter-compare-', dir=args.directory) as folder:
                    for phase, round_number in phases:
                        if args.evict_cache:
                            for path in Path(folder).rglob('*'):
                                if path.is_file():
                                    fd = os.open(path, os.O_RDONLY)
                                    try:
                                        os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
                                    finally:
                                        os.close(fd)
                        output = subprocess.check_output([
                            executable, engine, mode, phase, folder, str(args.count), str(size), str(round_number)], timeout=1800)
                        record = json.loads(output)
                        record['repeat'] = repeat
                        results.append(record)
                print(f'{size} bytes, {mode}, repeat {repeat}, {engine}: passed', flush=True)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            report = {**metadata, 'complete': len(results) == expected, 'results': results}
            args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(f'Results: {args.output}')
