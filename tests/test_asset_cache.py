"""Exercise preview caching, content invalidation and maintenance across processes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

example, cli = [str(Path(p).resolve()) for p in sys.argv[1:3]]
with tempfile.TemporaryDirectory(prefix='shutter-assets-') as folder:
    root = Path(folder)
    source, output, cache = root / 'source', root / 'output', root / 'cache.shdb'
    source.mkdir()

    def ppm(color):
        return b'P6\n# fixture\n512 256\n255\n' + bytes(color) * (512 * 256)

    def run():
        return json.loads(subprocess.check_output([example, str(cache), str(source), str(output)], timeout=30))

    originals = []
    for i in range(4):
        data = ppm((10 + i, 20, 30))
        if i == 3:
            data = b'P6\n512 256\n255\n' + bytes(channel for y in range(256) for x in range(512)
                                                   for channel in (x % 256, y, 10))
        originals.append(data)
        (source / f'{i}.ppm').write_bytes(data)
    first = run()
    assert (first['generated'], first['hits'], first['keys']) == (4, 0, 4)
    previews = {}
    for i in range(4):
        preview = (output / f'{i}.ppm').read_bytes()
        pixels = bytes((10 + i, 20, 30)) * (128 * 64)
        if i == 3:
            pixels = bytes(channel for y in range(64) for x in range(128)
                           for channel in ((4 * x + 1) % 256, 4 * y + 1, 10))
        assert preview == b'P6\n128 64\n255\n' + pixels
        previews[i] = preview
    second = run()
    assert (second['generated'], second['hits']) == (0, 4)
    assert all((output / f'{i}.ppm').read_bytes() == previews[i] for i in range(4))
    modified = source / '0.ppm'
    timestamp = modified.stat().st_mtime_ns
    modified.write_bytes(ppm((240, 20, 30)))
    os.utime(modified, ns=(timestamp, timestamp))
    third = run()
    assert (third['generated'], third['hits'], third['keys']) == (1, 3, 5)
    assert (output / '0.ppm').read_bytes() != previews[0]
    (source / 'duplicate.ppm').write_bytes((source / '1.ppm').read_bytes())
    assert run()['hits'] == 5
    old_key = 'preview-v1:128:' + hashlib.sha256(originals[0]).hexdigest()
    subprocess.run([cli, 'delete', old_key, '--db', str(cache)], check=True)
    before = cache.stat().st_size
    subprocess.run([cli, 'compact', '--db', str(cache)], check=True)
    assert cache.stat().st_size < before
    last = run()
    assert (last['generated'], last['hits'], last['keys']) == (0, 5, 4)
    batch_source = root / 'batch-source'
    batch_source.mkdir()
    for i in range(130):
        header = b'P6\n1 1\n255\r' if i == 10 else b'P6\r\n1 1\r\n255\r\n'
        (batch_source / f'{i}.ppm').write_bytes(header + bytes((i, 10, 32)))
    batch_args = [example, str(root / 'batch.shdb'), str(batch_source), str(root / 'batch-output')]
    batch_first = json.loads(subprocess.check_output(batch_args, timeout=30))
    batch_second = json.loads(subprocess.check_output(batch_args, timeout=30))
    assert (batch_first['generated'], batch_second['hits'], batch_second['keys']) == (130, 130, 130)
    assert (root / 'batch-output' / '10.ppm').read_bytes() == b'P6\n1 1\n255\n\x0a\x0a\x20'
    for broken in [b'', b'P6\n0 1\n255\n', b'P6\n8192 8192\n255\n',
                   b'P6\n1 1\n255\n\x00\x01', b'P6\n1 1\n65535\n\x00' * 2]:
        (source / 'broken.ppm').write_bytes(broken)
        result = subprocess.run([example, str(cache), str(source), str(output)], capture_output=True, timeout=30)
        assert result.returncode == 1 and result.stderr
print('Preview cache passed: restart, exact pixels, SHA-256 invalidation, reuse, deletion and compaction')
