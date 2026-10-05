# Benchmarks

The benchmark executable measures nine workloads and writes one JSON result per run.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_BENCHMARKS=ON
cmake --build build-release --config Release --parallel
build-release/shutter_bench --count 10000 --key-size 16 --value-size 128 --directory /var/tmp
build-release/shutter_bench --count 1000 --key-size 16 --value-size 128 --sync --directory /var/tmp
```

With Visual Studio, use `build-release/Release/shutter_bench.exe` and a local data directory.

## Workloads

Each run uses one thread and seed 20261005. Keys are fixed-width decimal IDs; values are
repeated `v` bytes. The index holds up to 2N keys.

1. Sequential PUT of N keys.
2. Random PUT of N additional keys.
3. Shuffled GET hits.
4. Shuffled GET misses.
5. Overwrite the first key set.
6. Delete the second key set.
7. Close and reopen to rebuild the index.
8. Verify the full log.
9. Compact the log.

Timings include key construction, allocation, checksums and I/O. Initial database creation
is excluded. Reopen, verification and compaction each report one operation; compare their
durations. Compaction includes verification, syncs, backup copying, replacement and cleanup.

## Measurement conditions

`--sync` includes a synchronization call in each write. Buffered runs sync once outside
the write timings, before reopen. Reads and reopen use warm OS caches.

Run at least three repetitions without concurrent builds or tests. Retain the raw JSON
and report medians and ranges. Record CPU, RAM, OS, filesystem, storage, compiler, flags,
dataset size, key/value sizes and write mode.

The [v0.1.0 results](benchmark-results.md) contain nine runs across three configurations.
For cross-engine comparisons, match durability, checksumming, cache state, dataset,
thread count and compaction accounting.
