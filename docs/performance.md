# Performance

ShutterDB v0.2.1 cache and storage measurements, with SQLite and RocksDB comparisons.
Results use a Ryzen 7 / WSL2 system, three repetitions and synchronization every 128 writes.

## Bounded cache

Each run inserts 4,096 unique assets into a 64 MiB `Cache`. The 4 KiB values fit;
64 KiB values force eviction and automatic compaction. Write timings include both.
Seconds and worst-call milliseconds: median (minimum–maximum) across three runs.

| Values | Retained keys | Evicted keys | All writes, s | Writes with compaction, s | Worst write, ms |
|---|---:|---:|---:|---:|---:|
| 4 KiB | 4,096 | 0 | 0.178 (0.167–0.184) | 0.000 (0.000–0.000) | 8.4 (4.9–9.1) |
| 64 KiB | 1,000 | 3,096 | 3.845 (3.352–4.564) | 3.331 (2.887–3.987) | 220.4 (145.1–256.1) |

The 64 KiB workload triggers compaction on 24 of 4,096 writes. Those calls account
for 87% of write time. Compaction verifies the old and new logs and writes every
live value into the replacement; it pauses the owning handle while it runs.

Reads return owned buffers after a verification pass. Each run measures three shuffled
hit passes and one evicted-key pass. The table shows medians of the per-run latency
percentiles. Reopening times handle construction in the same process with a warm filesystem cache.

| Values | Hit p50, µs | Hit p99, µs | Evicted-key miss p50, µs | Reopen, ms |
|---|---:|---:|---:|---:|
| 4 KiB | 3.70 | 13.48 | — | 8.98 |
| 64 KiB | 17.91 | 42.75 | 0.26 | 15.62 |

Input generation, statistics and byte comparisons are outside the timers.
The [benchmark guide](benchmarks.md#bounded-cache) describes verification and JSON fields.

## Storage comparison

These workloads use `DB`; cache-budget enforcement, eviction, image decoding and
hashing are excluded. Each dataset has 4,096 binary assets: 16 MiB of 4 KiB values
or 256 MiB of 64 KiB values. Two rounds of updates and deletions precede compaction.

### Batched ingestion

Seconds: median (minimum–maximum), including creation, writes, synchronization and
close/checkpoint. Every stored value is verified outside the timings.

| Initial values | ShutterDB | SQLite WAL | SQLite tuned | RocksDB |
|---|---:|---:|---:|---:|
| 16 MiB / 4 KiB each | 0.139 (0.126–0.143) | 0.260 (0.217–0.275) | 0.229 (0.225–0.260) | 0.217 (0.191–0.249) |
| 256 MiB / 64 KiB each | 0.554 (0.554–0.692) | 1.910 (1.827–2.126) | 2.168 (1.914–2.287) | 1.338 (1.184–1.371) |

### Warm reads and opening

Opening is measured separately. Reads cover three shuffled passes: 12,288 successful
lookups after one complete warm-up pass. Each adapter returns an owned value buffer.
The initial dataset has no obsolete records. Times are median milliseconds.

| Values | Engine | Open | Three read passes | Read process peak RSS, MiB |
|---|---|---:|---:|---:|
| 4 KiB | ShutterDB | 8.41 | 34.81 | 10.4 |
| 4 KiB | SQLite WAL | 14.80 | 87.69 | 12.4 |
| 4 KiB | SQLite tuned | 15.04 | 36.08 | 28.2 |
| 4 KiB | RocksDB | 161.31 | 26.27 | 30.6 |
| 64 KiB | ShutterDB | 75.37 | 230.11 | 10.5 |
| 64 KiB | SQLite WAL | 13.97 | 540.42 | 12.0 |
| 64 KiB | SQLite tuned | 17.77 | 148.30 | 266.1 |
| 64 KiB | RocksDB | 442.54 | 903.50 | 94.5 |

RSS includes resident mapped database pages and excludes the OS filesystem cache.
Both datasets fit within the 6.69 GiB available to WSL. ShutterDB validates the complete
log and rebuilds its index on open, so opening becomes slower as the log grows.

### Repeated updates and maintenance

Each round overwrites half the keys, removes one quarter and leaves one quarter unchanged.
Overwrites change a generation byte; the rest of each value stays the same.
The second round reinserts the deleted quarter before deleting it again. Churn totals
both rounds' open, write/sync and close times; it excludes the separate read phases.
There are 3,072 live values after both rounds. Durations are median seconds.

| Values | Engine | Both update rounds | Open after updates | Explicit compaction | Closed file after compaction, MiB |
|---|---|---:|---:|---:|---:|
| 4 KiB | ShutterDB | 0.242 | 0.015 | 0.068 | 12.30 |
| 4 KiB | SQLite WAL | 0.295 | 0.017 | 0.132 | 13.72 |
| 4 KiB | SQLite tuned | 0.285 | 0.014 | 0.123 | 13.72 |
| 4 KiB | RocksDB | 0.316 | 0.116 | 0.070 | 12.56 |
| 64 KiB | ShutterDB | 1.343 | 0.148 | 0.669 | 192.30 |
| 64 KiB | SQLite WAL | 0.499 | 0.015 | 1.956 | 193.72 |
| 64 KiB | SQLite tuned | 0.578 | 0.017 | 2.211 | 193.72 |
| 64 KiB | RocksDB | 1.674 | 0.144 | 0.723 | 192.63 |

RocksDB's explicit compaction time covers the remaining flush/CompactRange work after
background maintenance in earlier phases, including untimed verification. SQLite runs
VACUUM and a WAL checkpoint. ShutterDB verifies both logs, creates a synchronized
rollback backup and replaces the file. The backup uses a hard link on this filesystem;
filesystems without link support use a full copy. File totals include sidecars, WALs,
manifests and SST files after close.

### Filesystem-cache eviction requests

A separate 256 MiB run requested `POSIX_FADV_DONTNEED` on each database file before
every phase. The hint affects the guest filesystem cache; the virtual disk and host
may retain data. The table includes open plus the first complete lookup pass, because
ShutterDB's log scan itself reads the values into the filesystem cache.
Seconds: median (minimum–maximum), before updates.

| Engine | Open | First 4,096 lookups | Open + first pass |
|---|---:|---:|---:|
| ShutterDB | 0.466 (0.195–0.678) | 0.089 (0.075–0.092) | 0.555 (0.270–0.770) |
| SQLite WAL | 0.019 (0.017–0.021) | 0.420 (0.404–0.425) | 0.439 (0.422–0.446) |
| SQLite tuned | 0.023 (0.021–0.028) | 0.330 (0.328–0.521) | 0.351 (0.351–0.549) |
| RocksDB | 0.762 (0.394–1.049) | 1.506 (0.814–1.549) | 2.268 (1.208–2.598) |

## Method

- One client thread; random 64-byte ASCII keys; a pseudorandom value template stamped
  with each key's ID and generation; seed 20261005. RocksDB may use background workers.
  Engine order reverses on alternate repetitions.
- ShutterDB uses buffered writes with `sync()` every 128 operations. SQLite commits
  transactions of 128 operations with WAL and `synchronous=FULL`. RocksDB writes
  `WriteBatch` groups of 128 operations with `WriteOptions.sync=true`. SQLite and
  RocksDB provide atomic batches; ShutterDB can recover a prefix of an interrupted batch.
- SQLite 3.53.4 is built with `-O3 -DNDEBUG -DSQLITE_THREADSAFE=1`. Both configurations
  use prepared statements, a rowid table with a text primary key and a 2 MiB page cache.
  The tuned variant adds exclusive locking before accessing the WAL and a 256 MiB map.
  The ordinary variant disables memory mapping.
- RocksDB 11.8.1 uses default options except creation and disabled compression.
  It is a portable Release static build with compression libraries disabled. Reads
  copy a `PinnableSlice` into the same owned-buffer shape as the other adapters.
- ShutterDB checks CRC32C on every hit and scans every record on open. RocksDB uses
  its default checksumming; SQLite uses its native integrity behavior. No application
  checksum is added to the other engines.
- AMD Ryzen 7 5825U, 16 logical CPUs exposed, Ubuntu 26.04.1 under WSL2, 6.69 GiB RAM.
  GCC 15.2.0; CMake Release (`-O3 -DNDEBUG`); SSE4.2 CRC32C on ShutterDB. Data is on
  ext4 under `/var/tmp` on a WSL virtual disk; executables are in build directories
  on the mounted Windows filesystem. Runs execute sequentially on a host without
  workload isolation.
- Every storage-comparison phase uses a fresh process and verifies every expected
  value and deletion. Compaction verifies values before and after. Each bounded-cache
  run uses a fresh process; its read and handle-reopen phases share that process.

## Reproduce

Build and run the bounded-cache measurements:

```sh
cmake -S . -B build-cache -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_BENCHMARKS=ON
cmake --build build-cache --config Release --parallel
python3 tools/bench-cache.py build-cache/shutter_cache_bench \
  --directory /var/tmp --writes 4096 --sizes 4096 65536 --max-mib 64 \
  --sync-every 128 --repeats 3 --output cache.json
```

For the storage comparison, install SQLite and RocksDB development packages, then:

```sh
cmake -S . -B build-compare -DCMAKE_BUILD_TYPE=Release \
  -DSHUTTER_BUILD_COMPARISON=ON -DSHUTTER_COMPARE_ROCKSDB=ON
cmake --build build-compare --config Release --parallel
python3 tools/compare-asset-cache.py build-compare/shutter_compare \
  --directory /var/tmp --engines shutter sqlite sqlite-tuned rocksdb \
  --count 4096 --sizes 4096 65536 --modes batch --rounds 2 --repeats 3 \
  --output storage.json
```

Repeat with `--sizes 65536 --rounds 0 --evict-cache` for the initial-dataset
eviction-hint run on Linux. Place data on the filesystem being measured.
For custom dependency builds, set `CMAKE_PREFIX_PATH` for RocksDB and
`SQLite3_INCLUDE_DIR` / `SQLite3_LIBRARY` for SQLite. Use `--modes sync` to
synchronize every individual operation. Visual Studio executables are under `Release/`.

Raw data: [bounded cache](measurements/cache.json) · [storage](measurements/storage.json) ·
[filesystem-cache eviction requests](measurements/storage-eviction.json) ·
[environment and dependency builds](measurements/environment.json).
