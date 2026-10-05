# Changelog

## Unreleased

- Reorganize documentation into installation, API, CLI and storage references.
- Simplify command-line help and release reporting.

## 0.1.0 — 2026-10-05

First experimental release. C++20, format v1, MIT.

- Append-only storage for binary keys and values, with tombstones and an in-memory key index.
- CRC32C checksums on file headers, record headers and payloads.
- Synchronous and buffered writes, tail recovery and typed errors.
- Manual compaction with verified replacement and backup recovery.
- CLI with binary I/O, JSON diagnostics, verification and recovery.
- CMake install/export and local FetchContent support.

Release fixes: preserve unsupported-format files during backup recovery, reject FIFO
inspection without blocking, validate sequences in partial tails, and include incomplete-tail
categories and offsets in JSON diagnostics.

[Release notes](docs/releases/v0.1.0.md) · [Test results](docs/validation.md)
