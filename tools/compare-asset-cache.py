"""Compare asset-cache workloads in fresh processes, alternating engine order."""
import argparse
import hashlib
import json
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
parser.add_argument('--engines', nargs='+', choices=['shutter', 'sqlite', 'sqlite-tuned'],
                    default=['shutter', 'sqlite', 'sqlite-tuned'])
parser.add_argument('--engine-ref', help='Git revision of the engine compiled into the executable')
args = parser.parse_args()
if args.repeats < 1 or args.count < 4 or args.count > 100000 or args.count % 4:
    parser.error('positive repeats and a count divisible by four (4..100000) are required')
executable = str(args.executable.resolve())
repo = Path(__file__).resolve().parents[1]
engine_ref = None
if args.engine_ref:
    engine_ref = subprocess.check_output(['git', 'rev-parse', args.engine_ref + '^{commit}'], cwd=repo).decode().strip()
source_hashes = {}
for directory in ['src', 'include']:
    for path in sorted((repo / directory).rglob('*')):
        if path.is_file():
            relative = path.relative_to(repo).as_posix()
            contents = (subprocess.check_output(['git', 'show', engine_ref + ':' + relative], cwd=repo)
                        if engine_ref else path.read_bytes())
            source_hashes[relative] = hashlib.sha256(contents).hexdigest()
metadata = {
    'system': platform.platform(), 'seed': 20261005, 'count': args.count,
    'repeats': args.repeats, 'sizes': args.sizes, 'modes': args.modes, 'engines': args.engines,
    'executable_sha256': hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
    'engine_ref': engine_ref, 'source_sha256': source_hashes,
    'benchmark_sha256': hashlib.sha256((repo / 'benchmarks/asset_cache.cpp').read_bytes()).hexdigest(),
}
expected = len(args.sizes) * len(args.modes) * args.repeats * len(args.engines) * 4
results = []
for size in args.sizes:
    for mode in args.modes:
        for repeat in range(1, args.repeats + 1):
            engines = args.engines.copy()
            if repeat % 2 == 0:
                engines.reverse()
            for engine in engines:
                with tempfile.TemporaryDirectory(prefix='shutter-compare-', dir=args.directory) as folder:
                    for phase in ['populate', 'read', 'churn', 'reopen']:
                        output = subprocess.check_output([
                            executable, engine, mode, phase, folder, str(args.count), str(size)], timeout=600)
                        record = json.loads(output)
                        record['repeat'] = repeat
                        results.append(record)
                print(f'{size} bytes, {mode}, repeat {repeat}, {engine}: passed', flush=True)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            report = {**metadata, 'complete': len(results) == expected, 'results': results}
            args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(f'Results: {args.output}')
