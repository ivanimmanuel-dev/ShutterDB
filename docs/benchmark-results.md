# Local benchmark results

Measured on 2026-10-05. Three full runs per configuration; all nine raw JSON files and [environment metadata](measurements/environment.json) are retained. This is a small warm-cache development baseline, not an engine comparison.

Machine: AMD Ryzen 7 5825U, 16 exposed logical processors, 6.69 GiB WSL RAM; Ubuntu 26.04.1 / WSL2; GCC 15.2.0, Release `-O3 -DNDEBUG`. Data: ext4 on `/dev/sdd`, a WSL virtual disk, under `/var/tmp`. The physical storage device was not identified. No sanitizers were enabled. OS caches were not evicted.

**Virtualized synchronization timings must not be presented as native-device durability measurements.** Physical power-loss behavior was not tested. Buffered writes omit per-write flushes; their timings exclude the final explicit sync. Compaction includes the durable backup protocol.

## buffered-10k-16-128

N = 10,000; key = 16 B; value = 128 B; sync = false.

| Workload | Median | Min–max |
|---|---:|---:|
| sequential_put | 138,736.69 ops/s | 118,997.98–144,365.82 ops/s |
| random_put | 130,877.08 ops/s | 124,769.34–141,513.28 ops/s |
| random_get | 199,727.83 ops/s | 190,537.10–219,538.49 ops/s |
| missing_get | 7,131,899.49 ops/s | 5,848,479.92–7,909,390.03 ops/s |
| overwrite | 124,987.64 ops/s | 119,292.19–139,887.48 ops/s |
| delete | 137,730.52 ops/s | 132,028.08–141,943.09 ops/s |
| startup_recovery | 190.73 ms | 186.84–202.85 ms |
| compaction | 316.65 ms | 304.36–426.13 ms |

Compaction: 6,080,032 → 1,840,032 bytes, retaining 10,000 live keys.

## buffered-2k-32-4096

N = 2,000; key = 32 B; value = 4096 B; sync = false.

| Workload | Median | Min–max |
|---|---:|---:|
| sequential_put | 45,690.24 ops/s | 37,935.50–49,690.17 ops/s |
| random_put | 44,846.05 ops/s | 34,855.82–45,824.46 ops/s |
| random_get | 58,762.48 ops/s | 55,158.51–69,441.91 ops/s |
| missing_get | 8,956,801.35 ops/s | 5,056,365.84–9,416,949.57 ops/s |
| overwrite | 48,278.53 ops/s | 27,561.06–48,359.01 ops/s |
| delete | 128,031.68 ops/s | 105,393.45–163,160.26 ops/s |
| startup_recovery | 97.34 ms | 90.06–112.87 ms |
| compaction | 199.88 ms | 192.94–326.64 ms |

Compaction: 25,152,032 → 8,336,032 bytes, retaining 2,000 live keys.

## sync-1k-16-128

N = 1,000; key = 16 B; value = 128 B; sync = true.

| Workload | Median | Min–max |
|---|---:|---:|
| sequential_put | 547.90 ops/s | 467.75–570.44 ops/s |
| random_put | 554.97 ops/s | 540.39–580.78 ops/s |
| random_get | 237,015.63 ops/s | 199,913.08–238,051.65 ops/s |
| missing_get | 9,554,840.00 ops/s | 5,841,974.59–9,999,000.10 ops/s |
| overwrite | 559.20 ops/s | 540.27–562.80 ops/s |
| delete | 475.79 ops/s | 379.59–566.16 ops/s |
| startup_recovery | 17.38 ms | 16.61–24.47 ms |
| compaction | 46.52 ms | 35.59–91.48 ms |

Compaction: 608,032 → 184,032 bytes, retaining 1,000 live keys.

## Interpretation

This baseline confirms that all eight benchmark paths execute and verify their output. Small datasets, warm page cache and a virtual disk limit generalization. No p95/p99 latency, cold-cache throughput, scaling, memory-RSS or comparative performance claims are made. Run the [methodology](benchmarks.md) on the deployment environment before making performance decisions.
