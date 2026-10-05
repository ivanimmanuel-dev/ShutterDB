# Changelog

## 0.2.0 — 2026-10-05

- Add `shutter::Cache` with a disk budget, access-based eviction and automatic compaction.
- Buffer sequential log scans and reuse index entries during replay, retaining full checksum validation.
- Add a content-addressed PNG/JPEG preview cache with transparency and restart reuse.
- Accelerate CRC32C with runtime-detected x86-64 instructions and a portable slicing-by-eight fallback.
- Fetch and validate each value with one record read and one allocation.
- Add a reproducible SQLite/RocksDB comparison for ingestion, reads, updates and compaction.

Format v1 is unchanged. [Release notes](docs/releases/v0.2.0.md).

## 0.1.0 — 2026-10-05

First experimental release.

- Append-only storage for binary keys and values, with tombstones and an in-memory key index.
- CRC32C checksums on file headers, record headers and payloads.
- Synchronous and buffered writes, tail recovery and typed errors.
- Manual compaction with verified replacement and backup recovery.
- CLI with binary I/O, JSON diagnostics, verification and recovery.
- CMake install/export and local FetchContent support.

Release fixes: preserve unsupported-format files during backup recovery, reject FIFO
inspection without blocking, validate sequences in partial tails, and include incomplete-tail
categories and offsets in JSON diagnostics.

[Release notes](docs/releases/v0.1.0.md) · [Test results](https://github.com/ivanimmanuel-dev/ShutterDB/blob/v0.1.0/docs/validation.md)
