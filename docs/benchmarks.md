# Reproducible benchmarks

ShutterDB uses a dependency-free `steady_clock` harness. It times coarse workloads, checks results, prints one JSON object per invocation and deletes only its uniquely created temporary directory. It is a sensible starting point for trend measurements, not a statistically rigorous database ranking.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_BENCHMARKS=ON
cmake --build build-release --config Release --parallel
build-release/shutter_bench --count 10000 --key-size 16 --value-size 128 --directory /var/tmp
build-release/shutter_bench --count 1000 --key-size 16 --value-size 128 --sync --directory /var/tmp
```

## Workloads and timing boundaries

All workloads are single-threaded. The seed is 20261005. Keys are fixed-width padded decimal IDs; values are repeated `v` bytes. No compression is used. A run performs sequential PUT of N new keys, random PUT of another N disjoint keys, shuffled successful GET, shuffled missing GET, overwrite of the first set, DELETE of the second set, close/reopen recovery, a full verification scan, then compaction. Timings include key construction, allocations, checksums and engine I/O. The map may contain up to 2N keys.

PUT/GET/overwrite/delete rates are operation counts divided by elapsed seconds. Recovery, verification and compaction each have one operation; their durations are more informative than their rates. Compaction includes full original verification, temporary verification, syncs, backup copying, replacement and cleanup. Initial DB creation is outside timed workloads.

Buffered runs do not sync each write. An explicit `sync()` **outside the write timings** occurs before reopen. Do not compare those rates to synchronous durability. Synchronous runs include the synchronization cost in each write. Reads and reopen are warm-cache: the harness does not evict OS caches. There is no cold-start or tail-latency claim.

Run at least three replicates per configuration. Retain all raw runs; report medians and ranges. Record CPU, RAM, storage/filesystem, virtualization, OS/kernel, compiler, flags, key/value sizes, count and sync mode. Avoid parallel builds or test jobs during measurement. A virtual disk's reported device model is not evidence of physical storage hardware or durable flush completion.

## Measured results

See [the local benchmark report](benchmark-results.md) and the complete JSON runs in `docs/measurements/`. These are measurements on the development machine, not universal product claims. They include multiple sizes and both durability modes. No LevelDB/RocksDB/LMDB comparisons are made.

Before comparing other engines, match persistence guarantees, checksumming, dataset, cache state, compaction accounting, number of threads and transaction boundaries. Do not quietly batch one engine and sync every operation in another.
