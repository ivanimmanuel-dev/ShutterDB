# v0.1.0 benchmark results

Measured 2026-10-05. Three runs per configuration; tables show medians and ranges.
The nine raw JSON files and [environment metadata](measurements/environment.json) are retained.

| Environment | Configuration |
|---|---|
| CPU | AMD Ryzen 7 5825U, 16 logical processors |
| RAM | 6.69 GiB exposed to WSL |
| OS | Ubuntu 26.04.1 under WSL2 |
| Compiler | GCC 15.2.0, Release, `-O3 -DNDEBUG` |
| Storage | ext4 on WSL virtual disk `/dev/sdd`, data under `/var/tmp`; physical device unidentified |
| Cache | Warm OS cache, no eviction |
| Writes | Sync mode flushes each write; buffered timings exclude the final explicit sync |

Compaction includes verification, synchronization and backup copying.
See [methodology](benchmarks.md) for workload definitions and reproduction commands.
These timings describe this virtualized environment.

## buffered-10k-16-128

N = 10,000; key = 16 B; value = 128 B; sync = false.

| Workload | Median | Min–max |
|---|---:|---:|
| sequential_put | 170,889.18 ops/s | 164,388.23–180,543.62 ops/s |
| random_put | 169,173.79 ops/s | 154,670.18–179,110.27 ops/s |
| random_get | 255,681.85 ops/s | 248,165.57–292,644.20 ops/s |
| missing_get | 8,485,089.15 ops/s | 7,824,995.54–9,543,541.93 ops/s |
| overwrite | 168,772.53 ops/s | 151,449.42–186,543.10 ops/s |
| delete | 177,341.33 ops/s | 164,974.62–193,355.40 ops/s |
| startup_recovery | 142.75 ms | 126.41–143.08 ms |
| verification | 136.78 ms | 130.12–145.78 ms |
| compaction | 207.99 ms | 203.86–211.74 ms |

Compaction: 6,080,032 → 1,840,032 bytes, retaining 10,000 live keys.

## buffered-2k-32-4096

N = 2,000; key = 32 B; value = 4096 B; sync = false.

| Workload | Median | Min–max |
|---|---:|---:|
| sequential_put | 58,503.92 ops/s | 53,922.43–59,901.92 ops/s |
| random_put | 59,157.53 ops/s | 58,145.07–60,298.99 ops/s |
| random_get | 81,164.26 ops/s | 80,929.78–82,714.11 ops/s |
| missing_get | 10,910,598.56 ops/s | 10,558,602.89–10,983,217.64 ops/s |
| overwrite | 60,657.12 ops/s | 52,621.34–63,431.74 ops/s |
| delete | 197,320.95 ops/s | 175,437.66–197,634.69 ops/s |
| startup_recovery | 75.45 ms | 74.36–100.88 ms |
| verification | 75.68 ms | 75.46–103.66 ms |
| compaction | 170.49 ms | 153.23–182.60 ms |

Compaction: 25,152,032 → 8,336,032 bytes, retaining 2,000 live keys.

## sync-1k-16-128

N = 1,000; key = 16 B; value = 128 B; sync = true.

| Workload | Median | Min–max |
|---|---:|---:|
| sequential_put | 684.46 ops/s | 666.47–688.57 ops/s |
| random_put | 640.61 ops/s | 629.68–675.63 ops/s |
| random_get | 282,077.90 ops/s | 261,937.34–289,234.43 ops/s |
| missing_get | 11,979,204.10 ops/s | 11,233,556.88–12,066,073.82 ops/s |
| overwrite | 640.63 ops/s | 445.59–693.04 ops/s |
| delete | 692.77 ops/s | 659.28–727.48 ops/s |
| startup_recovery | 12.18 ms | 11.91–12.48 ms |
| verification | 12.09 ms | 12.03–12.20 ms |
| compaction | 29.11 ms | 29.07–31.21 ms |

Compaction: 608,032 → 184,032 bytes, retaining 1,000 live keys.
