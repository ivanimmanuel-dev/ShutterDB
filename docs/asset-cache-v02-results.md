# Asset-cache benchmarks: v0.2.0

This workload stores 16,384 binary assets, synchronizes every 128 operations and
performs two rounds of overwrites and deletions before compaction. The two datasets
contain 64 MiB and 1 GiB of initial values. Results are from one Ryzen 7 / WSL2 system.
Tables report medians and ranges from three repetitions.

## Batched ingestion

Seconds: median (minimum–maximum) of three runs, including creation, writes,
synchronization and close/checkpoint. Every stored value is verified outside the timings.

| Initial values | ShutterDB | SQLite WAL | SQLite tuned | RocksDB |
|---|---:|---:|---:|---:|
| 64 MiB / 4 KiB each | 0.712 (0.557–0.755) | 1.346 (1.260–1.503) | 1.419 (1.049–1.425) | 1.217 (0.747–1.509) |
| 1 GiB / 64 KiB each | 3.871 (3.398–10.616) | 22.872 (20.870–24.875) | 16.728 (15.921–21.352) | 15.692 (14.225–17.612) |

## Warm reads and opening

Opening is measured separately. Reads cover three shuffled passes: 49,152 successful
lookups after one complete warm-up pass. Each adapter returns an owned value buffer.
The initial dataset has no obsolete records. Times are median milliseconds.

| Values | Engine | Open | Three read passes | Read process peak RSS, MiB |
|---|---|---:|---:|---:|
| 4 KiB | ShutterDB | 37.18 | 169.71 | 19.4 |
| 4 KiB | SQLite WAL | 0.66 | 417.36 | 19.7 |
| 4 KiB | SQLite tuned | 0.90 | 176.43 | 90.3 |
| 4 KiB | RocksDB | 360.11 | 511.30 | 85.3 |
| 64 KiB | ShutterDB | 366.57 | 1178.31 | 19.4 |
| 64 KiB | SQLite WAL | 1.62 | 2642.24 | 19.1 |
| 64 KiB | SQLite tuned | 0.72 | 1579.46 | 275.1 |
| 64 KiB | RocksDB | 922.73 | 4202.90 | 100.3 |

RSS includes resident mapped database pages and excludes the OS filesystem cache.
Both datasets fit within the 6.69 GiB available to WSL.

## Repeated updates and maintenance

Each round overwrites half the keys, removes one quarter and leaves one quarter unchanged.
Overwrites change a generation byte; the rest of each value stays the same.
The second round reinserts the deleted quarter before deleting it again. Churn totals
both rounds’ open, write/sync and close times; it excludes the separate read phases.
There are 12,288 live values after both rounds. All durations are median seconds.

| Values | Engine | Both update rounds | Open after updates | Explicit compaction | Closed file after compaction, MiB |
|---|---|---:|---:|---:|---:|
| 4 KiB | ShutterDB | 1.201 | 0.094 | 0.807 | 49.22 |
| 4 KiB | SQLite WAL | 1.035 | 0.001 | 0.674 | 54.89 |
| 4 KiB | SQLite tuned | 0.988 | 0.001 | 0.626 | 54.89 |
| 4 KiB | RocksDB | 1.089 | 0.272 | 0.000 | 49.49 |
| 64 KiB | ShutterDB | 8.939 | 1.468 | 25.144 | 769.22 |
| 64 KiB | SQLite WAL | 2.511 | 0.001 | 19.297 | 774.89 |
| 64 KiB | SQLite tuned | 2.187 | 0.001 | 17.028 | 774.89 |
| 64 KiB | RocksDB | 27.268 | 0.123 | 7.073 | 769.98 |

RocksDB performs background flushes and compactions during other phases, including
untimed correctness checks. Its explicit compaction column measures the remaining
flush/CompactRange work, not all lifetime maintenance. SQLite uses VACUUM and a WAL
checkpoint. ShutterDB verifies the old log and replacement and durably copies a backup
before publication. At this size, that full-file protocol is a substantial maintenance cost.
File totals include regular sidecars, WALs, manifests and SST files after close.

## Reopen improvement

The buffered scanner was also compared directly with commit `87e4eae` using the same
database file. Each group is primed once; old and new engines run in fresh processes
with alternating order. Both validate the complete log and every resulting value.
Milliseconds: median (minimum–maximum) of three paired runs.

| Values | Log state | Before | Buffered scanner | Speedup |
|---|---|---:|---:|---:|
| 4 KiB | Initial | 92.40 (86.20–100.14) | 34.87 (32.90–44.15) | 2.65× |
| 4 KiB | After two update rounds | 217.76 (216.73–227.61) | 65.11 (62.58–103.75) | 3.34× |
| 64 KiB | Initial | 328.50 (290.50–379.56) | 268.80 (247.77–285.16) | 1.22× |
| 64 KiB | After two update rounds | 892.41 (834.77–988.46) | 545.53 (531.69–610.22) | 1.64× |

Opening remains proportional to log size. Buffering reduces small reads and allocations
while retaining complete checksum validation.

## Filesystem-cache eviction requests

A separate 1 GiB run requested `POSIX_FADV_DONTNEED` on each database file before every
phase. The hint affects the guest filesystem cache; the virtual disk and host may retain
data. The table includes open plus the first complete lookup pass,
because ShutterDB’s log scan itself reads the values into the filesystem cache.
Seconds: median (minimum–maximum), before updates.

| Engine | Open | First 16,384 lookups | Open + first pass |
|---|---:|---:|---:|
| ShutterDB | 1.216 (0.827–1.792) | 0.566 (0.296–0.569) | 1.785 (1.124–2.358) |
| SQLite WAL | 0.003 (0.001–0.007) | 1.810 (1.655–2.869) | 1.812 (1.658–2.876) |
| SQLite tuned | 0.001 (0.001–0.001) | 1.197 (1.120–1.530) | 1.198 (1.121–1.531) |
| RocksDB | 1.322 (1.255–1.485) | 10.678 (8.973–15.118) | 12.164 (10.295–16.373) |

## Method

- One client thread; 16,384 random 64-byte ASCII keys; a pseudorandom value template
  stamped with each key’s ID and generation; seed 20261005. RocksDB may use background
  worker threads. Engine order reverses on alternate repetitions.
- ShutterDB uses the ordinary `DB` API. These timings do not include `Cache` eviction
  bookkeeping, image decoding or hashing. The [image integration test](testing.md)
  separately checks automatic eviction and budget enforcement.
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
  GCC 15.2.0; CMake Release (`-O3 -DNDEBUG`); SSE4.2 CRC32C on ShutterDB. Executables
  and data are on ext4 under `/var/tmp`, on a WSL virtual disk. The physical drive
  was not identified. ShutterDB measurements ran sequentially, with its builds/tests
  paused. Other QEMU workloads were observed on the host; hardware was not isolated.
- Three repetitions per configuration. Every phase uses a fresh process and verifies
  every expected value and deletion. Compaction verifies values before and after.

## Reproduce

Install SQLite and RocksDB development packages, then:

```sh
cmake -S . -B build-compare -DCMAKE_BUILD_TYPE=Release \
  -DSHUTTER_BUILD_COMPARISON=ON -DSHUTTER_COMPARE_ROCKSDB=ON
cmake --build build-compare --config Release --parallel
python3 tools/compare-asset-cache.py build-compare/shutter_compare \
  --directory /var/tmp --engines shutter sqlite sqlite-tuned rocksdb \
  --count 16384 --sizes 4096 65536 --modes batch --rounds 2 --repeats 3 \
  --output comparison.json
```

Repeat with `--sizes 65536 --rounds 0 --evict-cache` for the initial-dataset
eviction-hint run on Linux. Zero rounds skips the update and compaction phases.
Place the executable and data on the filesystem being measured. For custom dependency
builds, set `CMAKE_PREFIX_PATH` for RocksDB, and `SQLite3_INCLUDE_DIR` / `SQLite3_LIBRARY`
for SQLite. The source hashes and dependency build details below identify these results.
The runner also supports `--modes sync` to synchronize every individual operation;
the [earlier report](asset-cache-results.md#synchronizing-each-write) measures that workload.

For a paired reopen comparison, compile this version’s `benchmarks/asset_cache.cpp`
against each engine revision. Populate and update one database with the current binary,
then alternate the binaries on the same file using `shutter batch read DIRECTORY 16384
65536 0` before updates, or `shutter batch reopen DIRECTORY 16384 65536 2` after two
churn rounds. Prime each group once and retain three measured runs per binary.

Raw data: [v0.2](measurements/asset-cache-v02-after.json) ·
[eviction requests](measurements/asset-cache-v02-evicted.json) ·
[paired reopening](measurements/asset-cache-v02-reopen.json) ·
[earlier engine, separate run](measurements/asset-cache-v02-before.json) ·
[environment and dependency builds](measurements/asset-cache-v02-environment.json).
