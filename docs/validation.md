# v0.1.0 test results

Release date: 2026-10-05. Commit: `4e7f884a3dbabfa429ba9d7a08be159d28323f98`.

## Build and integration

All 12 [release-commit CI jobs](https://github.com/ivanimmanuel-dev/ShutterDB/actions/runs/37281939602)
passed. The [package workflow](https://github.com/ivanimmanuel-dev/ShutterDB/actions/runs/37282305611)
also passed for Linux and Windows x64.

| Check | Result |
|---|---|
| Hosted Linux | GCC and Clang, Debug and Release |
| Hosted Windows | MSVC, Debug and Release |
| Hosted macOS | Release |
| Local Linux engine | 30 cases, 6,351 assertions; 6 CTest suites passed |
| Local Windows engine | 26 cases, 6,248 assertions; 5 suites passed in Debug and Release |
| ASan and UBSan | No findings in local or hosted runs |
| ThreadSanitizer | Shared-handle concurrent-call test passed |
| Formatting and analysis | clang-format 21 and configured clang-tidy checks passed |
| Integration | Installed and local FetchContent consumers passed on Linux and Windows |
| README example | Extracted, compiled and executed against the installed library |

The source archive was reproduced byte-for-byte on Linux and Windows, then built and
tested from a clean extraction. Downloaded native packages passed the CLI demo and
relocated CMake consumer tests. Final CI, package and archive records are attached to
the [release](https://github.com/ivanimmanuel-dev/ShutterDB/releases/tag/v0.1.0).

## Fuzzing

Clang 21.1.8 with ASan/UBSan; 180-second requested budget per target, 1 MiB input limit
and 512 MiB RSS limit. Total: **8,878,774 executions; zero crashes or sanitizer findings**.

| Target | Duration | Executions | Active corpus | Crashes |
|---|---:|---:|---:|---:|
| record | 181 s | 5,602,470 | 41 | 0 |
| scanner | 181 s | 977,044 | 78 | 0 |
| recovery | 181 s | 1,152,020 | 79 | 0 |
| verify | 181 s | 1,147,240 | 77 | 0 |

`scanner`, `recovery` and `verify` share the production scanner. Fuzz inputs are in memory;
syscall behavior is covered separately. Build activity overlapped some runs.

[Results](measurements/release-fuzz.json) ·
[Record log](measurements/fuzz/record.log) ·
[Scanner log](measurements/fuzz/scanner.log) ·
[Recovery log](measurements/fuzz/recovery.log) ·
[Verify log](measurements/fuzz/verify.log) ·
[Corpus archive](https://github.com/ivanimmanuel-dev/ShutterDB/releases/download/v0.1.0/ShutterDB-0.1.0-fuzz-corpus.zip)

## Stress and memory

1,000,000 mixed operations over 100,000 distinct keys, using seed 20261005. Ten mutation
processes compare every key with an oracle and verify the log. Separate processes compact,
reopen and run the memory probe. All 13 passed locally; the workload also passed hosted CI.
Writes were buffered, with sync every 10,000 operations and at batch boundaries.

| Measurement | Result |
|---|---:|
| Live keys after workload | 66,861 |
| File before / after compaction | 85,979,147 / 11,553,073 bytes |
| Compaction | 10.405 s |
| Open before / after compaction | 3.398 / 0.530 s |
| Mutation-loop throughput, including syncs | 106,585 ops/s |
| Full runner, including checks and memory probe | 73.067 s |
| Maximum mutation-process RSS, including oracle | 24,512 KiB |
| 256 MiB value probe: baseline / peak RSS | 16,168 / 16,168 KiB |

The oracle stores generations and presence flags; it regenerates expected values.
[Per-process measurements](measurements/release-stress.json) include counts and timings.

## Failure and corruption coverage

- Eleven exception and process-exit boundaries across append, sync and compaction.
- Twenty-four Linux syscall cases: short I/O, EINTR, ENOSPC, read/write/truncate errors,
  sync failures, replacement failures and initial publication.
- Twenty-eight CLI corruption cases covering headers, checksums, payloads, lengths,
  sequences, truncation, garbage and unsupported versions; each checks offsets and input preservation.
- Every-byte mutation and every-truncation engine tests.
- Compaction with 5,000 hot-key overwrites, 2,000 tombstones, binary/empty values,
  a 65,536-byte key, a 16 MiB value, repeated compaction and reopen.

Acknowledged values and tombstones survived the tested failures. Complete records with
invalid checksums were rejected; incomplete tails recovered the valid prefix.
[Corruption diagnostics](measurements/corruption-diagnostics.json) retain example output.

## Measurement environment

Local Linux: Ubuntu 26.04.1 under WSL2, kernel 6.18.33.2, GCC 15.2 and Clang 21.1.8.
CPU: Ryzen 7 5825U, 16 logical processors; 6.69 GiB RAM exposed to WSL.
Test data used ext4 under `/var/tmp` on virtual disk `/dev/sdd`; physical storage was not
identified. Local Windows used MSVC 19.51 and temporary directories.

The [benchmark report](benchmark-results.md) retains nine runs across three configurations,
with parameters, medians and ranges. Physical power cuts, sector tearing and reordered
block I/O were not tested. Platform synchronization details are in [durability](durability.md).
