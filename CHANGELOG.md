# Changelog

## 0.1.0 — 2026-10-05 — experimental

First public experimental release. Format v1; C++20; MIT; no third-party runtime dependency.

- Append-only binary key/value engine with tombstones and offset-based indexing.
- CRC32C protection for file metadata, record metadata and payloads; bounded decoding.
- Sync-by-default and explicit buffered mode; conservative tail recovery and typed errors.
- Manual compaction with a checked replacement, durable backup and stable process lock.
- CLI with JSON diagnostics, binary I/O, read-only verification and explicit recovery.
- CMake install/export and local FetchContent integration, tested in independent projects.
- Hosted Linux GCC/Clang, Windows MSVC and macOS tests; sanitizers, faults and fuzzing.

Release hardening fixed backup recovery overwriting an unsupported-format primary,
POSIX inspection blocking on a FIFO, and acceptance of an out-of-order sequence in a
partial tail. JSON tail diagnostics now include an error category and byte offset.
Added 1,000,000-operation validation, syscall failure coverage, 28 CLI corruption cases,
near-limit compaction tests and separately timed verification benchmarks.

This is not a production-durability certification. Physical power interruption, sector
tearing and reordered block I/O remain untested. See [validation](docs/validation.md),
[durability](docs/durability.md) and [release notes](docs/releases/v0.1.0.md).
