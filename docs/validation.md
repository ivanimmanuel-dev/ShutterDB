# v0.1.0 experimental validation — 2026-10-05

**The required experimental-release gates passed. No known serious release blocker remains
within the tested scope.** This is evidence from executed checks, not a claim of production
certification or physical power-loss safety.

## Hosted evidence

[Hardening CI run 37280253558](https://github.com/ivanimmanuel-dev/ShutterDB/actions/runs/37280253558)
passed all 12 jobs on commit `225078f8fe6422b169a8112be2b7028fcedfb2cd`:
Linux GCC/Clang Debug/Release, Windows MSVC Debug/Release, macOS, separate ASan and UBSan,
fuzz, formatting/static analysis, and the million-operation stress workload. Linux and
Windows jobs also compiled/ran installed and local FetchContent consumers outside the
source tree. The exact job outcomes are retained in
[hosted-hardening-ci.json](measurements/hosted-hardening-ci.json).

Release metadata and documentation are finalized afterward. The
[release workflow](../.github/workflows/release.yml) independently requires successful
`CI` on the **exact tagged commit** before building and publishing it. Refer to the
[Actions history](https://github.com/ivanimmanuel-dev/ShutterDB/actions) and the release
asset manifests for the tagged commit and final package run. A workflow file alone does
not satisfy this gate.

## Local execution

| Check | Executed result |
|---|---|
| GCC 15.2 Release, expanded suite | 6/6 CTest groups; 30 engine cases, 6,351 assertions |
| Windows MSVC 19.51 x64, Debug and Release | 5/5 groups each; 26 engine cases, 6,248 assertions |
| Clang 21.1.8 Debug, combined ASan/UBSan | 5/5 groups, no findings; includes compaction, corruption, recovery and process tests |
| GCC 15.2 ThreadSanitizer | Existing shared-handle concurrent-call test passed, no race diagnostic |
| clang-format 21.1.8 and configured clang-tidy | Passed; production library and CLI checked by the analyzer |
| Installed package, local FetchContent, exact README C++ | Separate temporary source/build directories; compiled and ran successfully |
| CLI demo | Independent process per command; see [recorded output](demo-transcript.txt) |
| Benchmark baseline | Nine complete runs: three configurations × three repetitions; nine workloads each |

Linux has four additional POSIX engine cases: process termination, cross-process/symlink
locking, hardlinks and FIFO inspection. The Linux syscall interposer is omitted from ASan
builds to avoid runtime interposition conflicts; it runs in ordinary and UBSan builds.
TSan was a targeted shared-handle check, not an exhaustive concurrency campaign.

## Timed fuzz campaign

Clang 21.1.8, ASan and UBSan, 180-second requested budget per target (181 seconds reported),
1 MiB maximum input, 512 MiB RSS limit. **8,878,774 executions, zero crashes and zero
sanitizer findings.** No fixes or crashing corpus were produced by this campaign.

| Target | Duration | Executions | Active corpus / files retained | Findings |
|---|---:|---:|---:|---:|
| record | 181 s | 5,602,470 | 41 / 85 | 0 |
| scanner | 181 s | 977,044 | 78 / 303 | 0 |
| recovery | 181 s | 1,152,020 | 79 / 301 | 0 |
| verify | 181 s | 1,147,240 | 77 / 292 | 0 |

[Machine-readable results](measurements/release-fuzz.json) and sanitized logs
([record](measurements/fuzz/record.log), [scanner](measurements/fuzz/scanner.log),
[recovery](measurements/fuzz/recovery.log), [verify](measurements/fuzz/verify.log)) preserve
the measurements. Corpus files are retained as a separate release evidence archive.
Scanner, verify and recovery share the production parser; these are not four independent
implementations. Disk syscalls are tested separately, not fuzzed. Some builds overlapped
these campaigns, so execution rates are not performance measurements. Multi-hour commands
are in [testing](testing.md).

## Million-operation workload and memory

Fixed seed 20261005; 1,000,000 mixed operations over 100,000 distinct keys, including
binary/empty values, overwrites, deletes, reads and misses. Ten independent mutation
processes plus compact, reopen-check and memory-probe processes all passed. Every batch
compares **all keys** against a generation/presence oracle and scans the full log. The
oracle regenerates values on demand rather than retaining their payloads.

- 85,979,147 bytes before compaction → 11,553,073 bytes afterward; 66,861 live keys intact.
- Compaction: 10.405 seconds. Startup before it: 3.398 seconds; afterward: 0.530 seconds.
- Mutation-loop throughput including explicit barriers: 106,585 operations/second;
  full runner, including scans/restarts/compaction/memory probe: 73.067 seconds.
- Mutation-process peak RSS: at most 24,512 KiB, including the oracle and runtime.
- A separate 4,096-key workload wrote 256 MiB of values; baseline and peak RSS were both
  16,168 KiB. This rules out retaining that payload volume in this workload, not an exact
  allocator bound or a general memory guarantee.

These are buffered writes with explicit `sync()` every 10,000 operations and at batch
boundaries, **not one million individually synchronous commits**. No power failure occurs
in this workload. Raw per-process results are in [release-stress.json](measurements/release-stress.json).
The same workload also passed on hosted Ubuntu outside WSL; its job uploads separate data.

## Crash, fault, corruption and compaction

Eleven exception/process-exit boundaries cover before/during/after append, before/after
sync, output creation, validation, replacement and directory sync. Every reopen checks
acknowledged seed values and tombstones. Buffered data may disappear on OS/power failure;
the process tests do not emulate that event. Complete unacknowledged records may appear.

Twenty-four Linux syscall scenarios exercise short reads/writes, EINTR, ENOSPC, failed
reads/writes/truncation, file and directory sync failures, failed replacement and failed
initial publication. Failed replacement retains the original bytes; all reopened cases
verify and retain acknowledged data. A failed flush demonstrably leaves an uncertain
complete write, which is documented rather than falsely described as rollback.

Twenty-eight CLI corruption cases cover file/record magic, metadata and payload CRCs,
key/value bytes, checked malformed lengths, zero/duplicate/decreasing sequences, middle
damage, final-header/payload truncation, zero/random garbage and unsupported versions.
Verification and strict reopening return categorized errors at checked offsets without
altering input. Recovery accepts only recognized tails. Preserved
[diagnostics](measurements/corruption-diagnostics.json) supplement every-byte mutation and
every-truncation engine tests. No malformed-file crash or unreasonable allocation occurred.

Compaction torture covers 5,000 hot-key overwrites, 2,000 tombstones, binary keys/values,
empty values/databases, a 65,536-byte key, a 16 MiB value, repeated compaction and reopen.
Latest bytes remain exact, deleted keys stay absent, stale records disappear, and resulting
files pass complete verification. Sequence exhaustion and checksummed duplicate records
fail closed.

## API and repository review

Construction/open policy, put/get/get_string/remove/contains, sync/stats/compact,
verify/inspect/recovery, Options and errors were reviewed. Inputs need remain alive only
during calls; returned values own their bytes. Copy and move of DB are deliberately
disabled. No API redesign was needed. The public surface remains four headers; CMake
exports `ShutterDB::ShutterDB`. Pre-1.0 API compatibility is explicitly limited.

All project-owned sources, build/install paths, CLI, tests, documentation and workflow
files were inspected. No project-owned TODO/FIXME marker remains; upstream doctest TODOs
are preserved with its pinned unmodified header and license. The missing coverage at
baseline (syscall returns, large workload, compaction limits, future-version recovery,
FIFO inspection, external consumers and separately timed verification) was addressed.
Source packaging uses Git objects, excluding build output, caches and local secrets.

## Environment and practical limits

Local Linux: Ubuntu 26.04.1 on WSL2 kernel 6.18.33.2, AMD Ryzen 7 5825U, 16 logical CPUs,
6.69 GiB exposed RAM, ext4 `/var/tmp` on a virtual disk reported as `/dev/sdd`. Physical
storage hardware was not identified. Windows checks used local temporary directories.
Benchmarks ran sequentially after CPU-heavy checks, without sanitizers, with warm OS
caches; [all parameters and raw runs](benchmark-results.md) are retained. These are
baseline measurements, not comparisons with other engines.

Physical power cuts, sector tearing, block reordering, additional filesystem/device
combinations, long-term soak and external independent durability review remain unverified.
Windows has no identical directory-fsync primitive here; macOS uses fsync, not F_FULLFSYNC.
CRC32C is not authentication, and whole-record suffix loss cannot be detected without
external history. Network filesystems, rogue writers and deleting sidecars while open are
unsupported. Read the full [durability contract](durability.md).

## Explicit release gate

| Question | Answer |
|---|---|
| Hosted Linux CI green? | Yes: GCC/Clang Debug/Release and fault/integration suites |
| Hosted Windows CI green? | Yes: MSVC Debug/Release and CLI/consumers |
| ASan clean? | Yes, in executed local and hosted workloads |
| UBSan clean? | Yes, in executed local and hosted workloads |
| Fuzz smoke/extended run clean? | Yes: hosted timed smoke and 4 × 181 s local campaigns |
| Persistence across process restart? | Yes: CLI, stress, consumer and abrupt-exit tests |
| Million-operation stress passed? | Yes: 100,000 keys; local and hosted |
| Corruption cases handled? | Yes: 28 CLI scenarios plus mutation/truncation engine tests |
| Compaction failure recovery tested? | Yes: exceptions, process exits and syscall failures |
| Independent CMake consumer works? | Yes: installed and local FetchContent, Linux and Windows |
| README quick start verified? | Yes: clean hosted builds, actual extracted example and CLI demo |
| Benchmarks reproducible? | Yes: commands, parameters, nine raw runs and environment retained |
| Known data-loss bug? | None known within the documented assumptions |
| Known corruption bug? | None known |
| Known UB? | None known |
| Known crash on malformed database? | None known |
| Known release blocker? | None known for this experimental scope |

The absence of a known bug is not a proof of absence. Final tagging and publishing still
require exact-commit CI and successful fresh artifact builds, enforced by the release workflow.
