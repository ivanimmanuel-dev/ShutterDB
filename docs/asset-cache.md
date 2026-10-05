# PNG/JPEG preview cache

The example creates PNG previews of PNG and JPEG sources, with the longest edge at
most 128 pixels. A SHA-256 content key lets it reuse previews across process restarts
and share entries between identical files. The cache enforces a disk budget and
automatically evicts old entries and reclaims obsolete records.

## Build and run

The example uses OpenSSL, libpng 1.6+ and libjpeg. On Ubuntu:

```sh
sudo apt-get install libssl-dev libpng-dev libjpeg-dev python3-pil
cmake -S . -B build-assets -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_ASSET_CACHE=ON
cmake --build build-assets --config Release --parallel
build-assets/shutter_asset_cache previews.shdb ./images ./previews --max-mib 1024
```

Pillow is used by the integration tests.

The program reads a flat input directory containing `.png`, `.jpg` or `.jpeg` files
(case insensitive). Each image may be up to 4096 × 4096 pixels and 64 MiB encoded.
The output directory must be separate from the inputs and database directory.

Outputs retain the original filename and append `.png`: `photo.jpg → photo.jpg.png`.
This keeps `photo.jpg` and `photo.png` distinct. Small images are not enlarged.
Downsampling averages pixels with alpha weighting, so transparent borders do not
introduce colored fringes. Metadata such as EXIF orientation is not applied.

The optional `--max-mib` argument defaults to 1024. The budget covers the database file;
generated output files are separate. Compaction needs temporary disk space beyond
the steady-state budget. See [cache behavior](cache.md#budget-and-eviction).

## Reuse and invalidation

Each run prints JSON with generated previews, hits, stored keys, database bytes,
budget, evictions and elapsed time. Run it again to reuse the stored previews.
Timing includes open, source reads and hashing, rendering on misses, cache maintenance,
synchronization and output writes.

The storage calls in [examples/asset_cache.cpp](../examples/asset_cache.cpp) are:

```cpp
auto preview = cache.get_string(key);
if (!preview) {
    preview = Image(source).preview();
    cache.put(key, *preview);
}
```

The key is `preview-v2:128:<sha256>`. A content change produces a new key even if the
file size and timestamp remain unchanged. Changing the renderer should change the
version prefix. Writes synchronize every 128 generated previews and at the end.

## Inspect the cache

```sh
shutter stats --db previews.shdb
shutter verify --db previews.shdb
```

The [storage comparison](performance.md) measures ShutterDB, SQLite and
RocksDB without image decoding or hashing in the storage timings.
