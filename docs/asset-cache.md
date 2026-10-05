# Preview cache

The example builds 128-pixel previews of P6 PPM images and stores them in ShutterDB.
It hashes the source bytes with SHA-256, so an unchanged image reuses its preview across
process restarts. Identical files share an entry, even under different names. A content
change generates a new preview regardless of the file's timestamp.

## Build and run

The example uses OpenSSL for SHA-256. On Ubuntu, install `libssl-dev`, then build:

```sh
cmake -S . -B build-assets -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_ASSET_CACHE=ON
cmake --build build-assets --config Release --parallel
build-assets/shutter_asset_cache previews.shdb ./images ./previews
```

Use a directory of binary P6 `.ppm` images, up to 4096 × 4096 pixels with 8-bit RGB.
The output directory must be separate from the inputs and database directory.
The longest preview edge is at most 128 pixels; the example averages source pixels
within each output pixel. It writes the previews as PPM files.

Each run prints JSON with generated previews, cache hits, stored keys, cache bytes
and elapsed time. Run it again to reuse the stored previews. The timing includes
opening the database, reading and hashing sources, and writing output files.

The storage calls are in [examples/asset_cache.cpp](../examples/asset_cache.cpp):

```cpp
auto preview = db.get_string(key);
if (!preview) {
    preview = Image(source).preview();
    db.put(key, *preview);
}
```

The key includes the renderer version, output size and source digest:
`preview-v1:128:<sha256>`. Changing the renderer should change its version prefix.
The example buffers writes and calls `sync()` every 128 new previews and at the end.
After a crash, any lost previews can be regenerated from the source images.

## Maintenance

Content changes create new entries. When an old asset version expires, remove its key
and compact the log to reclaim its bytes:

```sh
shutter delete 'preview-v1:128:<old-sha256>' --db previews.shdb
shutter compact --db previews.shdb
shutter verify --db previews.shdb
```

The example leaves expiry decisions to the application. The integration test covers
exact preview pixels, restart reuse, content changes with an unchanged timestamp,
identical-file reuse, deletion and compaction:

```sh
ctest --test-dir build-assets -R asset_cache --output-on-failure
```

## Storage performance

The [SQLite comparison](asset-cache-results.md) measures the storage operations on
4–64 KiB binary assets. Image loading, hashing and rendering are outside those timings.
