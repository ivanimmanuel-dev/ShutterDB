# Contributing

For bugs, include a minimal reproduction, the ShutterDB version, compiler, OS and
filesystem. For feature proposals, describe the application problem and expected behavior.
Use [private reporting](SECURITY.md) for security-sensitive defects.

## Development checks

1. Build Debug and Release and run CTest.
2. Add regression tests for changed behavior. Storage changes need restart and failure cases.
3. Run ASan/UBSan for storage changes and libFuzzer for parser changes.
4. Format project C and C++ with clang-format 21 and run the configured clang-tidy checks.
5. Update the API, format or durability documentation when behavior changes.
6. Run the installed-package consumer for build-system changes.

Commands are in [testing](docs/testing.md). Tests use temporary databases. Benchmark
reports include parameters, environment details and raw measurements.

Keep doctest pinned and retain its license when updating it. The library and CLI have
no third-party runtime dependencies; see [third-party notices](THIRD_PARTY.md).

Pull requests should describe the change and list the checks performed.
Contributions are licensed under [MIT](LICENSE).
