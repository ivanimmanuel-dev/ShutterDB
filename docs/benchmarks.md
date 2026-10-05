# Benchmarks

See [Performance](performance.md) for the SQLite/RocksDB comparison and commands to
reproduce it.

Build both standalone benchmarks:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_BENCHMARKS=ON
cmake --build build-release --config Release --parallel
```

With Visual Studio, executables are in `build-release/Release/`. Use a local data directory.

## Bounded cache

Measure `Cache` as it fills its budget, evicts entries and compacts automatically:

```sh
build-release/shutter_cache_bench --writes 4096 --value-size 65536 \
  --max-mib 64 --sync-every 128 --directory /var/tmp
python3 tools/bench-cache.py build-release/shutter_cache_bench \
  --writes 4096 --sizes 4096 65536 --max-mib 64 --sync-every 128 \
  --repeats 3 --directory /var/tmp --output cache.json
```

Each write inserts a unique 64-byte key. The 4 KiB workload fits within this budget;
the 64 KiB workload forces eviction and maintenance. Values are pseudorandom bytes
stamped with their key ID, using seed 20261005.

`insert` includes automatic eviction, compaction and every scheduled `sync()`.
`maintenance_insert` is the subset of writes that exceed the file budget and trigger
compaction. A final partial batch's sync is reported separately. Each group reports
total time and nearest-rank p50, p95, p99 and maximum call latency.

Reads cover three shuffled passes over retained entries and one pass over evicted keys.
`reopen_seconds` times construction of a new cache handle after closing the old one,
in the same process with a warm filesystem cache.
Key/value generation, statistics checks and full byte comparisons are outside the timers.
The benchmark checks the file budget after every write, verifies all surviving and
evicted keys before and after reopening, and finishes with full file verification.

The Python runner uses a fresh process for each size and repetition, alternates size
order, and records source and executable hashes alongside the results.

## Database operations

`shutter_bench` measures nine workloads and writes one JSON result per run:

```sh
build-release/shutter_bench --count 10000 --key-size 16 --value-size 128 --directory /var/tmp
build-release/shutter_bench --count 1000 --key-size 16 --value-size 128 --sync --directory /var/tmp
```

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
durations. Compaction includes verification, syncs, backup creation, replacement and cleanup.

## Measurement conditions

`--sync` includes a synchronization call in each write. Buffered runs sync once outside
the write timings, before reopen. Reads and reopen use warm OS caches.

Compare runs with the same write mode, dataset and cache state. Report the CPU, RAM,
filesystem, compiler and build flags alongside timings.
