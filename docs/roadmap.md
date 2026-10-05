# Roadmap and release gate

## Current: 0.1.0-dev

Persistent PUT/GET/DELETE, restart scanning, checksummed format, bounded parsing, manual compaction with a durable backup, synchronization modes, typed errors, locking, CLI, tests, sanitizer/fuzz targets, benchmarks and CMake packaging are implemented. This does **not** make the v0.1 release complete.

Before publishing v0.1:

- Complete hosted Linux/Windows CI on the exact tagged commit.
- Review format v1 and recovery choices independently; explicitly freeze the format or mark migration requirements.
- Run longer fuzz campaigns and real disk/syscall fault injection, including ENOSPC, sync/rename failures and reordered writes.
- Exercise Linux crash behavior outside WSL and on more than one local filesystem/device; investigate power-cut behavior.
- Measure larger datasets, memory use and startup costs; collect external integration feedback.
- Verify source archives, license notices, docs examples and package-consumer tests from the archive itself.

## Only after measurements

Potential v0.2 work: read concurrency, lock hardening, better memory/startup statistics and measured cache improvements. Potential v0.3 work: segmented logs and incremental startup work. Potential later work: snapshots and atomic batches, then only justified transaction experiments.

No LSM/SSTable rewrite, network mode, replication, GUI, cloud APIs or automatic roadmap implementation is planned for this preview. Each proposal must show a concrete need, a bounded design and a failure-testing strategy.
