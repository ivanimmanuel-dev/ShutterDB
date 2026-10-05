"""Check PNG/JPEG previews, content invalidation, restart reuse and bounded caching."""
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

from PIL import Image

example, cli = [str(Path(p).resolve()) for p in sys.argv[1:3]]
with tempfile.TemporaryDirectory(prefix='shutter-assets-') as folder:
    root = Path(folder)
    source, output, cache = root / 'source', root / 'output', root / 'cache.shdb'
    source.mkdir()

    def run():
        return json.loads(subprocess.check_output([example, str(cache), str(source), str(output)], timeout=60))

    originals = []
    for i in range(4):
        image = Image.new('RGB', (512, 256), (10 + i, 20, 30))
        if i == 3:
            image = Image.frombytes('RGB', (512, 256), bytes(
                channel for y in range(256) for x in range(512) for channel in (x % 256, y, 10)))
        image.save(source / f'{i}.png', compress_level=0)
        originals.append((source / f'{i}.png').read_bytes())
    Image.new('RGB', (256, 512), (80, 140, 200)).save(source / '0.jpg', quality=95)
    alpha = Image.frombytes('RGBA', (256, 128), bytes(
        channel for y in range(128) for x in range(256)
        for channel in ((255, 0, 0, 255) if x % 2 == 0 else (0, 0, 255, 0))))
    alpha.save(source / 'alpha.PNG')
    first = run()
    assert (first['generated'], first['hits'], first['keys']) == (6, 0, 6)
    previews = {}
    for i in range(4):
        with Image.open(output / f'{i}.png.png') as preview:
            assert preview.size == (128, 64) and preview.format == 'PNG'
            pixels = bytes((10 + i, 20, 30, 255)) * (128 * 64)
            if i == 3:
                pixels = bytes(channel for y in range(64) for x in range(128)
                               for channel in ((4 * x + 1) % 256, 4 * y + 1, 10, 255))
            assert preview.convert('RGBA').tobytes() == pixels
        previews[i] = (output / f'{i}.png.png').read_bytes()
    with Image.open(output / '0.jpg.png') as jpeg_preview:
        assert jpeg_preview.size == (64, 128)
        assert all(abs(actual - expected) <= 2
                   for actual, expected in zip(jpeg_preview.getpixel((30, 60)), (80, 140, 200, 255)))
    with Image.open(output / 'alpha.PNG.png') as alpha_preview:
        assert alpha_preview.convert('RGBA').tobytes() == bytes((255, 0, 0, 127)) * (128 * 64)
    second = run()
    assert (second['generated'], second['hits']) == (0, 6)
    assert all((output / f'{i}.png.png').read_bytes() == previews[i] for i in range(4))
    modified = source / '0.png'
    timestamp = modified.stat().st_mtime_ns
    Image.new('RGB', (512, 256), (240, 20, 30)).save(modified, compress_level=0)
    assert modified.stat().st_size == len(originals[0])
    os.utime(modified, ns=(timestamp, timestamp))
    third = run()
    assert (third['generated'], third['hits'], third['keys']) == (1, 5, 7)
    assert (output / '0.png.png').read_bytes() != previews[0]
    (source / 'duplicate.png').write_bytes((source / '1.png').read_bytes())
    assert run()['hits'] == 7
    old_key = 'preview-v2:128:' + hashlib.sha256(originals[0]).hexdigest()
    subprocess.run([cli, 'delete', old_key, '--db', str(cache)], check=True)
    before = cache.stat().st_size
    subprocess.run([cli, 'compact', '--db', str(cache)], check=True)
    assert cache.stat().st_size < before
    last = run()
    assert (last['generated'], last['hits'], last['keys']) == (0, 7, 6)

    batch_source = root / 'batch-source'
    batch_source.mkdir()
    for i in range(130):
        Image.new('RGB', (1, 1), (i, 10, 32)).save(batch_source / f'{i}.png')
    batch_args = [example, str(root / 'batch.shdb'), str(batch_source), str(root / 'batch-output')]
    batch_first = json.loads(subprocess.check_output(batch_args, timeout=60))
    batch_second = json.loads(subprocess.check_output(batch_args, timeout=60))
    assert (batch_first['generated'], batch_second['hits'], batch_second['keys']) == (130, 130, 130)
    with Image.open(root / 'batch-output' / '10.png.png') as tiny:
        assert tiny.size == (1, 1) and tiny.getpixel((0, 0)) == (10, 10, 32, 255)

    bounded_source = root / 'bounded-source'
    bounded_source.mkdir()
    random_bytes = random.Random(1729)
    for i in range(24):
        Image.frombytes('RGB', (128, 128), random_bytes.randbytes(128 * 128 * 3)).save(bounded_source / f'{i}.png')
    bounded_path = root / 'bounded.shdb'
    bounded_args = [example, str(bounded_path), str(bounded_source), str(root / 'bounded-output'), '--max-mib', '1']
    for _ in range(2):
        bounded = json.loads(subprocess.check_output(bounded_args, timeout=60))
        assert bounded['max_bytes'] == 1024 * 1024
        assert bounded['cache_bytes'] == bounded_path.stat().st_size <= bounded['max_bytes']
        assert bounded['evictions'] > 0 and bounded['generated'] + bounded['hits'] == 24
    assert len(list((root / 'bounded-output').glob('*.png'))) == 24
    subprocess.run([cli, 'verify', '--db', str(bounded_path)], check=True)

    bad_source = root / 'bad-source'
    bad_source.mkdir()
    bad_args = [example, str(root / 'bad.shdb'), str(bad_source), str(root / 'bad-output')]
    for name, broken in [('bad.png', b''), ('bad.png', originals[0][:100]),
                         ('bad.jpg', b'\xff\xd8bad'), ('bad.jpg', (source / '0.jpg').read_bytes()[:-20])]:
        bad_path = bad_source / name
        bad_path.write_bytes(broken)
        result = subprocess.run(bad_args, capture_output=True, timeout=30)
        assert result.returncode == 1 and result.stderr, (name, result.stdout, result.stderr)
        bad_path.unlink()
    Image.new('RGB', (4097, 1)).save(bad_source / 'too-wide.png')
    assert subprocess.run(bad_args, capture_output=True, timeout=30).returncode == 1
    assert subprocess.run(batch_args + ['--max-mib', '0'], capture_output=True, timeout=30).returncode == 1
print('Preview cache passed: PNG/JPEG pixels, alpha, restart, content changes, batching, eviction and malformed input')
