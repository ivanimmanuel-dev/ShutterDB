# Asset-cache comparison at 87e4eae

These results describe commit `87e4eae`. See the [v0.2 comparison](asset-cache-v02-results.md)
for larger datasets, repeated updates, RocksDB and the buffered scanner.

This run stores 2,048 random 64-byte keys with 4, 16 or 64 KiB values. Both engines synchronize every
128 writes. SQLite is also measured with exclusive locking and a 256 MiB memory map.

Across these three sizes, ShutterDB ingests batches 1.58–2.81× faster than the faster
SQLite configuration tested. SQLite opens existing databases faster, wins on larger
warm reads with memory mapping, and is faster when synchronizing every individual write.

## Batched ingestion

Times are milliseconds: median (minimum–maximum) of three runs. They include database
creation, all inserts, synchronization and close, including SQLite's final checkpoint.
Full-value correctness checks are outside the timings.

| Value size | ShutterDB | SQLite WAL | SQLite tuned | ShutterDB speedup vs faster SQLite configuration |
|---|---:|---:|---:|---:|
| 4 KiB | 63.5 (60.3–63.9) | 100.1 (97.3–105.0) | 100.8 (95.9–106.4) | 1.58× |
| 16 KiB | 102.3 (102.0–113.9) | 234.0 (229.7–234.4) | 239.9 (232.6–253.1) | 2.29× |
| 64 KiB | 284.3 (280.2–401.9) | 859.6 (815.5–1000.1) | 799.2 (772.2–799.3) | 2.81× |

## Reads, reopen and maintenance

These are medians from the batched databases. Warm reads cover 6,144 successful lookups
after one complete read pass. Reopen validates the ShutterDB log and rebuilds its index.
Compaction follows 1,024 overwrites and 512 deletes, with all remaining values checked
before and after. File sizes include sidecars.

| Value size | Operation | ShutterDB | SQLite WAL | SQLite tuned |
|---|---|---:|---:|---:|
| 4 KiB | Warm reads, ms | 12.13 | 38.41 | 14.90 |
| 4 KiB | Open existing file, ms | 8.15 | 0.53 | 0.37 |
| 4 KiB | Compaction, ms | 60.98 | 57.28 | 56.69 |
| 4 KiB | Read process peak RSS, MiB | 5.48 | 7.12 | 15.20 |
| 4 KiB | File after compaction, MiB | 6.15 | 6.87 | 6.87 |
| 16 KiB | Warm reads, ms | 29.28 | 62.25 | 22.71 |
| 16 KiB | Open existing file, ms | 13.85 | 0.47 | 0.42 |
| 16 KiB | Compaction, ms | 166.73 | 164.30 | 156.17 |
| 16 KiB | Read process peak RSS, MiB | 5.57 | 7.00 | 38.94 |
| 16 KiB | File after compaction, MiB | 24.15 | 24.87 | 24.87 |
| 64 KiB | Warm reads, ms | 121.58 | 155.76 | 55.04 |
| 64 KiB | Open existing file, ms | 52.47 | 0.68 | 0.37 |
| 64 KiB | Compaction, ms | 618.20 | 801.37 | 835.34 |
| 64 KiB | Read process peak RSS, MiB | 5.75 | 7.19 | 135.20 |
| 64 KiB | File after compaction, MiB | 96.15 | 96.87 | 96.87 |

RSS measures resident pages in the process, including SQLite's mapped database pages.
It excludes the OS filesystem cache used by both engines.

## Synchronizing each write

The same ingestion workload with a synchronization on every insert, in milliseconds:

| Value size | ShutterDB | SQLite WAL | SQLite tuned |
|---|---:|---:|---:|
| 4 KiB | 3475.4 (3262.0–3587.5) | 2161.2 (2160.0–2296.2) | 2283.0 (2040.6–2498.4) |
| 16 KiB | 3600.3 (3467.6–3605.0) | 2344.7 (2065.1–3200.9) | 2295.9 (2193.1–2621.7) |
| 64 KiB | 3702.5 (3687.7–3821.2) | 3011.7 (2889.6–3316.5) | 2844.4 (2757.8–3017.3) |

## Engine changes

The previous engine is commit `9e3b59a`, compiled with the same comparison harness.
The new engine selects the x86 CRC32C instruction when available, uses slicing-by-eight
CRC32C elsewhere, and reads each indexed record into one buffer. The file format and
sync behavior are unchanged. Both CRC paths are checked against an independent bitwise
implementation; the old and new engines also read each other's files.

Median batched ingestion and warm-read times, in milliseconds:

| Value size | Ingestion before | Ingestion after | Warm reads before | Warm reads after |
|---|---:|---:|---:|---:|
| 4 KiB | 85.11 | 63.49 | 76.01 | 12.13 |
| 16 KiB | 168.71 | 102.30 | 228.77 | 29.28 |
| 64 KiB | 557.58 | 284.30 | 866.30 | 121.58 |

## Method

- One thread; 2,048 keys; pseudorandom binary values; seed 20261005. Each phase starts
  in a fresh process. Engine order reverses on alternate repetitions.
- SQLite 3.53.4, built from its amalgamation with `-O3 -DNDEBUG -DSQLITE_THREADSAFE=1`.
  Both adapters return owned value buffers. SQLite uses prepared UPSERT, SELECT and
  DELETE statements, a rowid table with a text primary key, WAL, `synchronous=FULL`
  and a 2 MiB page cache. The tuned variant sets `locking_mode=EXCLUSIVE` before
  accessing the WAL and `mmap_size=268435456`; ordinary SQLite disables mapping.
- ShutterDB owns its database exclusively. Batched mode uses buffered writes plus
  `sync()` every 128 operations; SQLite uses transactions of 128 operations. The
  durability intervals match. SQLite also supplies atomic transactions; ShutterDB
  batches can recover a prefix after a crash. The cache can regenerate missing assets.
- ShutterDB verifies a record's CRC32C on every hit and checks the full log on open.
  SQLite uses its native integrity behavior, without an additional application checksum.
- The first read pass and subsequent three passes are recorded separately. OS caches
  are warm; no cache eviction is performed. Misses, churn, each phase's close, file sizes
  and peak RSS are retained in the raw results. These are storage timings; image hashing
  and rendering are measured separately by the [preview example](asset-cache.md).
- AMD Ryzen 7 5825U, 16 logical CPUs exposed, 6.69 GiB RAM available to Ubuntu under WSL2;
  GCC 15.2.0, CMake Release (`-O3 -DNDEBUG`). Executables and databases are on ext4
  under `/var/tmp`, backed by a WSL virtual disk. The physical drive was not identified.
  Runs execute sequentially without concurrent builds or tests.

SQLite's [synchronization](https://www.sqlite.org/pragma.html#pragma_synchronous),
[memory mapping](https://www.sqlite.org/pragma.html#pragma_mmap_size) and
[exclusive WAL locking](https://www.sqlite.org/wal.html#use_of_wal_without_shared_memory)
documentation describes the tested settings.

## Reproduce

Check out commit `87e4eae`, install SQLite development headers and build the optional comparison:

```sh
cmake -S . -B build-compare -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_COMPARISON=ON
cmake --build build-compare --config Release --parallel
python3 tools/compare-asset-cache.py build-compare/shutter_compare \
  --directory /var/tmp --output comparison.json
```

Place the build and data on the filesystem being measured. With Visual Studio, use
`build-compare/Release/shutter_compare.exe` and a local data directory. The runner accepts
`--count`, `--sizes`, `--repeats`, `--modes` and `--engines`; all results verify every value.
To select a particular SQLite build, supply `SQLite3_INCLUDE_DIR` and `SQLite3_LIBRARY`
to CMake. The recorded version and source hashes identify this run.

Raw evidence: [current engine](measurements/asset-cache-after.json),
[previous engine](measurements/asset-cache-before.json),
[environment and build details](measurements/asset-cache-environment.json).
