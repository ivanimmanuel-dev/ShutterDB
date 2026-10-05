# v0.1 release hardening ledger

Scope: validate and harden the existing engine. No roadmap features.

| Gate / finding | Baseline | Required evidence |
|---|---|---|
| Hosted CI | No remote or hosted runs | Exact candidate commit green on Linux and Windows, sanitizers, quality |
| Unsupported-format recovery | A backup could overwrite an unsupported primary | Fail closed; preserve both files; regression test |
| Nonregular input | POSIX read-only open could block on a FIFO before type validation | Nonblocking type probe; bounded subprocess regression |
| Partial tail sequence | Complete sequence field in a short header was not checked against the prefix | Reject non-increasing sequence without mutation |
| OS failure coverage | Only high-level injected exceptions | Short writes, EINTR, ENOSPC, sync, rename and directory-sync failures |
| Stress scale | 4,200 randomized operations | 1,000,000 operations, 100,000 keys, independent-process oracle checks, measurements |
| Fuzz duration | 20,000 executions per target | Timed ASan/UBSan campaign, executions and corpus retained |
| Compaction extremes | Mainly small strings | Repeated, binary, near-limit, heavily overwritten/tombstoned cases |
| Consumer integration | Installed sample under source tree | Standalone source outside tree; installed and local FetchContent modes |
| Benchmark completeness | Verification not separately timed | Add verification timing; keep all runs and parameters |
| Version/release artifacts | 0.1.0-dev | Evidence-based version, notes, reproducible archives, checksums and release gate |

This ledger is updated as evidence is collected. No hosted/experimental-release pass is inferred from workflow files alone.
