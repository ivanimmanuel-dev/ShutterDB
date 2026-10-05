# Changelog

## 0.1.0-dev — unreleased

- C++20 append-only key/value engine with binary values, tombstones and offset-based indexing.
- Format v1 with independent CRC32C protection for file metadata, record metadata and payloads.
- Synchronous and buffered writes; conservative tail recovery and typed diagnostics.
- Manual compaction, verified replacement and backup recovery under a stable process lock.
- CLI including JSON verification, binary I/O and explicit tail recovery.
- Offline tests, process-crash tests, parser smoke coverage and libFuzzer targets.
- CMake export/install/consumer support, reproducible benchmark harness and engineering documentation.

No production-readiness or hosted-CI pass claim is implied by this unreleased version.
