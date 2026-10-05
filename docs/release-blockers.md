# v0.1 release hardening ledger

Scope: validate and harden the existing engine. No roadmap features were added.

| Finding / gate | Resolution | Evidence |
|---|---|---|
| Unsupported-format recovery could replace a newer primary | Fixed: fail closed and preserve both files | Regression test with valid future-version header and backup |
| POSIX inspection could block opening a FIFO | Fixed: nonblocking open before regular-file validation | Bounded subprocess regression |
| Partial tail with a complete old sequence could be truncated | Fixed: reject non-increasing sequence | Regression asserts corruption and unchanged bytes |
| JSON tail report omitted category/offset | Fixed: explicit CORRUPTION and valid-prefix offset | 28 black-box corruption scenarios |
| OS error handling lacked meaningful coverage | Closed for release scope | 24 real syscall return-path scenarios; ENOSPC, EINTR, short I/O, sync, rename, truncate |
| Scale and resident-value behavior unmeasured | Closed | 1M operations / 100k keys / 13 processes; 256 MiB value probe |
| Only short fuzz runs | Closed for bounded release campaign | 8,878,774 ASan/UBSan executions across four timed targets |
| Compaction extremes | Closed | Hot-key/tombstone/binary/empty/max-key/max-value/repeated/reopen tests |
| External consumers | Closed | Installed and local FetchContent sources outside repository; local and hosted Linux/Windows |
| Verification benchmark missing | Closed | Separate timing; nine workloads × nine retained runs |
| Hosted CI unexecuted | Closed | All 12 jobs passed hardening run 37280253558; exact tag checked again before publishing |
| API/version/package consistency | Reviewed | VERSION/public string/CMake check; Git-based reproducible source archive; gated release workflow |

No known serious release blocker remains within the experimental scope. Full execution
details and remaining physical-device/long-term limits are in [validation](validation.md).
The release workflow must still pass on the final tagged commit; it cannot publish on
the strength of this ledger alone.
