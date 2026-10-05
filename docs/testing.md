# Tests, faults and fuzzing

The default build has offline doctest tests, a deterministic parser smoke test, a public API example, and a Python CLI integration suite when Python is available. Every test database uses a uniquely created temporary directory or filename.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/shutter_tests
```

Use `TMPDIR` to select the tested filesystem on POSIX. A tmpfs run is a useful parser/process test but says little about disk persistence. Linux validation used ext4 in WSL for test data, not the Windows source mount or tmpfs.

## Sanitizers

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DSHUTTER_ASAN=ON -DSHUTTER_UBSAN=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

The switches may be used separately. GCC is also supported. MSVC supports the ASan switch; UBSan requires GCC/Clang on POSIX. Sanitizer runs belong to development builds, not performance measurements. GCC ThreadSanitizer was also run successfully against the existing shared-handle concurrent-call test; this was a targeted check, not an exhaustive scheduling campaign.

## Deterministic failure coverage

The test-only library enables an internal, thread-local callback. Production `ShutterDB` compiles those hooks out. Tests inject failures before append, after a header-only append, after complete append, before/after sync, at compaction start, after temporary-record writes, at temporary validation, before/after replacement, and after directory sync. Every case closes and reopens the database and checks its logical state.

On POSIX, additional child processes call `_Exit` at all those boundaries so destructors and normal cleanup cannot conceal crash behavior. Recovery from a missing/corrupt primary with a valid backup, invalid temporary output, stale sidecars, strict tail mode, lock conflicts, aliases and randomized map-oracle workloads are covered. File corruption tests mutate every byte of a sample log and test every truncation point, along with explicitly checksummed malformed length/type/sequence fields.

On Linux, the default Python-enabled build also runs `syscall_faults`: a test-only
`LD_PRELOAD` interposer makes actual OS calls return short counts, EINTR, ENOSPC and EIO.
Twenty-four disposable scenarios cover append, read, truncate, initial publication and
compaction sync/rename boundaries. Acknowledged seed writes and tombstones must survive
every reopened case. The interposer is never linked into or installed with the library.
It is omitted from ASan builds to avoid competing runtime interposition; it runs under
UBSan and ordinary builds. Device reordering and physical power cuts remain untested.

The `corruption` suite invokes `shutter verify --json`, strict reopen and recovery against
28 independently damaged copies. It asserts categories, byte offsets, unchanged corrupt
input, and valid-prefix recovery for incomplete tails. Example output is preserved in
[corruption diagnostics](measurements/corruption-diagnostics.json).

## Million-operation release workload

```sh
cmake -S . -B build-stress -DCMAKE_BUILD_TYPE=Release -DSHUTTER_BUILD_STRESS=ON
cmake --build build-stress --parallel
python3 tools/run-stress.py build-stress/shutter_stress --directory /var/tmp --output stress.json
```

The fixed seed is 20261005. Ten independent processes perform 100,000 operations each
over 100,000 distinct keys; each checks all keys against a regenerated oracle and fully
verifies the file. Two more processes compact and recheck. A separate 256 MiB value probe
measures peak RSS. Values are binary, including empty values. The oracle retains only
generation/presence metadata. This is buffered mode with explicit sync every 10,000
operations and at batch boundaries, **not one million individually synced commits**.

## libFuzzer

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

Fuzzer targets instrument the engine with ASan/UBSan and coverage. The record target exercises the header decoder and payload checksum path. Scanner, recovery and verification targets share the production scanner and exercise strict/allow-tail policies plus report invariants. These three intentionally share a parser, so four binaries are not four independent implementations. Inputs are limited to 1 MiB by the combined harness; its index limit is 128 live keys and 1 MiB accounted memory. Parser callbacks do not create database files. libFuzzer manages corpus updates and can save crash artifacts in its working directory.

The portable `shutter_fuzz_smoke` runs 20,000 seeded random/structured mutations plus every truncation of a multi-record fixture. It is deterministic regression coverage, not a substitute for sustained coverage-guided fuzzing. Preserve minimized crashes as corpus regressions and extend targeted tests.

For a longer campaign, change `-max_total_time=180` to `-max_total_time=3600` or more,
reuse the generated corpus, and retain stdout/stderr and any crash artifact. The local
timed campaign and hosted 30-second-per-target smoke are recorded in [validation](validation.md).

## Formatting and analysis

Format project-owned C++ with clang-format 21. The third-party header is excluded. Use `-DSHUTTER_CLANG_TIDY=ON -DSHUTTER_BUILD_TESTS=OFF` to enable the checked-in analyzer configuration. CI checks GCC/Clang Debug and Release, sanitizer builds, Windows, macOS, formatting, static analysis and package consumption.
