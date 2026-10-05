# Contributing to ShutterDB

Start with a reproducible behavior or a measured need. Preserve simple integration, explicit durability and fail-closed corruption handling.

1. Build Debug and Release with CMake and run CTest.
2. For storage changes, add a targeted failure/restart case and run ASan/UBSan.
3. For parser changes, add a corpus/regression test and run the libFuzzer smoke targets.
4. Format project-owned C++ with clang-format 21; run the configured clang-tidy checks.
5. Update format/durability documentation whenever observable behavior changes.
6. Run the installed-package consumer before submitting a build-system change.

Tests must use temporary directories. Never run a corruption/recovery experiment on a user's original database. Benchmarks must include all parameters, hardware/environment information and raw results. Do not claim tests ran when only CI configuration was written.

The core library has no third-party runtime dependencies. doctest 2.4.12 is the only vendored dependency and is test-only; see [third-party notices](THIRD_PARTY.md). Do not modify its header without recording provenance and preserving the license.

Proposals should state the trigger, current behavior, intended behavior, failure modes and relevant validation. Defer features that substantially complicate the storage model without evidence of need. Contributions are made under the repository's MIT license.
