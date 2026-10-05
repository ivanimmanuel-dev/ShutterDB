# Testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

The C++ tests use vendored doctest. Python enables CLI, corruption and syscall tests.
All suites use temporary databases; set `TMPDIR` on POSIX to select the filesystem.

| Suite | Coverage |
|---|---|
| `engine` | Reads, writes, limits, locks, recovery, compaction, CRCs, scan boundaries and bounded-cache behavior |
| `parser_smoke` | 20,000 seeded mutations and every truncation of a fixture |
| `public_api_example` | Public headers and basic operations |
| `cli` | Separate-process persistence, binary I/O, JSON and exit codes |
| `corruption` | 28 damaged-file cases with checked diagnostics and recovery behavior |
| `syscall_faults` | 24 Linux scenarios for short I/O, EINTR, ENOSPC, sync, truncate and rename failures |
| `asset_cache` (optional) | PNG/JPEG pixels, alpha, restart reuse, content changes, batching, eviction and malformed inputs |

The engine tests inject failures at eleven append, sync and compaction boundaries.
POSIX tests also terminate child processes at those boundaries. The Linux syscall suite
uses a test-only interposer, excluded from installed builds and ASan configurations.

Enable `SHUTTER_BUILD_ASSET_CACHE` with OpenSSL, PNG and JPEG development libraries,
and Python Pillow installed to include the preview test.
CI also runs the [SQLite/RocksDB comparison](asset-cache-v02-results.md) on a small dataset and
checks every value after insertion, overwrites, deletion, compaction and reopening.

## Sanitizers

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DSHUTTER_ASAN=ON -DSHUTTER_UBSAN=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

ASan and UBSan can be enabled separately with GCC or Clang. MSVC supports the ASan option.
The syscall suite runs in ordinary and UBSan builds; ASan uses its own I/O interposition.

## Stress workload

```sh
cmake -S . -B build-stress -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_STRESS=ON
cmake --build build-stress --parallel
python3 tools/run-stress.py build-stress/shutter_stress --directory /var/tmp --output stress.json
```

Ten processes perform 1,000,000 total operations over 100,000 distinct keys, using seed
20261005. Each batch compares all keys with a regenerated oracle and verifies the log.
Separate processes compact, reopen and measure memory use with 256 MiB of values.
The workload uses buffered writes, with sync every 10,000 operations and at batch end.

## Fuzzing

```sh
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DSHUTTER_BUILD_FUZZERS=ON
cmake --build build-fuzz --parallel
python3 tools/fuzz-seeds.py build-fuzz/corpus
for target in record scanner recovery verify; do
  ./build-fuzz/fuzz_$target -max_total_time=180 -timeout=5 -max_len=1048576 \
    -rss_limit_mb=512 -print_final_stats=1 build-fuzz/corpus/$target
done
```

Targets use libFuzzer with ASan/UBSan. `record` exercises decoding and payload checksums;
`scanner`, `recovery` and `verify` share the production scanner and test its policies and
report invariants. The scanner harness limits inputs to 1 MiB and the index to 128 keys
and 1 MiB. Fuzz callbacks use in-memory input; syscall failures have a separate suite.

Increase `-max_total_time` for longer runs. Retain the corpus and minimized crash inputs,
and add a regression test for each fix. CI runs 30 seconds per target.

## Formatting and analysis

Format project-owned C and C++ with clang-format 21. Enable the configured clang-tidy checks
with `-DSHUTTER_CLANG_TIDY=ON -DSHUTTER_BUILD_TESTS=OFF`.

CI covers GCC/Clang Debug and Release, Windows MSVC, macOS, sanitizers, fuzzing, stress,
formatting, static analysis and installed/FetchContent consumers.

## Package checks

```sh
python3 tools/check-project.py
python3 tests/test_release.py
```

These check version consistency, release notes, local documentation links, reproducible
source archives and package checksums. Version checks also run under optimized Python.
The release workflow requires successful hosted CI on the tagged commit.
