# Validation record — 2026-10-05

**Result: working development preview, not a completed v0.1 release.** All local checks below were actually run. The GitHub Actions workflow is configured but has not run on a hosted repository. No physical power-loss test was performed.

## Local results

| Check | Environment | Result |
|---|---|---|
| Engine, parser smoke, public API example, CLI | GCC 15.2, Debug, Ubuntu 26.04.1 under WSL2 | 4/4 CTest groups passed |
| Same suite | GCC 15.2, Release (`-O3 -DNDEBUG`) | 4/4 passed |
| Same suite with AddressSanitizer + UndefinedBehaviorSanitizer | Clang 21.1.8, Debug, Linux | 4/4 passed; no sanitizer findings |
| Coverage-guided parser smoke | Clang 21.1.8 libFuzzer + ASan/UBSan | 20,000 inputs each for record, scanner, recovery and verify; all 4 completed |
| Deterministic parser smoke | Debug/Release and sanitized configurations | 20,000 mutations plus every truncation of the seed fixture passed |
| Windows engine, parser, API and CLI | MSVC 19.51, x64, Debug | 4/4 CTest groups passed |
| Same Windows suite | MSVC 19.51, x64, Release | 4/4 passed |
| Installed package consumed by a separate CMake project | Linux and Windows Release | Build and execution passed |
| Exact first C++ example extracted from README | Linux, separate installed-package consumer | Compiled and printed `world` |
| clang-tidy | Clang 21.1.8, production library and CLI | Passed configured checks with diagnostics treated as errors |
| clang-format | Version 21.1.8, project-owned C++ | Applied; checked-in style |
| Benchmarks | Linux Release, ext4 WSL virtual disk | Nine runs, three configurations, eight workloads per run; all completed and verified |

The detailed Linux engine run reported **25 test cases and 4,296 assertions**. Windows reported **22 test cases and 4,198 assertions**; the three additional POSIX cases exercise abrupt process exits, process/symlink locking and hardlink rejection. The randomized oracle performs 4,200 operations over repeated restarts and compactions. Counts describe this revision, not a promise of exhaustive coverage.

## Behavior demonstrated

- PUT/GET/overwrite/DELETE persist through independent opens and CLI processes.
- Binary keys/values, zero-length values and maximum-sized values work; invalid sizes fail before append.
- Every byte mutation in a sample database is rejected. Every truncation point is tested; only recognized incomplete record tails recover.
- A corrupted length in the middle fails its header checksum instead of triggering destructive tail recovery.
- Verification preserves input bytes and reports the first error offset and expected/actual CRC where applicable.
- Compaction discards dead history while retaining live values and the sequence watermark.
- Exception injection and abrupt POSIX process exits exercise append/flush/compaction boundaries, followed by reopen checks.
- Corrupt/missing primary files recover from a valid compaction backup; stale temporary output is handled under the stable lock.
- A competing handle/process cannot acquire the database lock; compaction retains lock ownership.
- Windows replacement initially failed with open data handles. The final implementation closes those handles while holding the stable sidecar lock; repeated-compaction tests pass on both platforms.

The [guided CLI transcript](demo-transcript.txt) demonstrates restart persistence, tombstones, corruption of a **copy**, verification, compaction and another reopen. Run it yourself with `python tools/demo.py PATH_TO_SHUTTER`.

## Environment and reproducibility

Linux test databases used `/var/tmp` on ext4 inside WSL2 (kernel 6.18.33.2-microsoft-standard-WSL2). Source/build files were on the Windows mount. Windows used local temporary directories. CPU: AMD Ryzen 7 5825U; WSL exposed 16 logical processors and roughly 6.69 GiB RAM. The virtual disk was reported as `/dev/sdd`; the physical device was not identified.

Build commands and sanitizer/fuzz instructions are in [testing](testing.md). Benchmark methodology, all raw measurements and environment metadata are in [benchmarks](benchmarks.md) and [benchmark results](benchmark-results.md). These timings are not native-device or comparative performance claims.

## What remains unverified or experimental

- Hosted Linux/Windows/macOS CI on a published commit; the workflow has only been inspected locally.
- Native Linux outside WSL, additional filesystems/devices, and physical power interruption.
- Exhaustive OS syscall errors, disk-full behavior, sector tearing and block-write reordering.
- Long-duration fuzzing, independent format/protocol review and external users' integration experience.
- macOS execution, Windows ASan and ThreadSanitizer campaigns.
- Package-manager publication (vcpkg, Conan, Homebrew, AUR), large-scale performance and memory profiling.

Do not interpret passing local tests as certification of production durability. Format migration and a stable-release support policy are still release decisions.
