# Third-party notices

The library and CLI depend on the C++ standard library and OS APIs.

| Dependency | Version | Use | License |
|---|---|---|---|
| [doctest](https://github.com/doctest/doctest/tree/v2.4.12) | 2.4.12 | Tests only | [MIT](third_party/doctest/LICENSE.txt) |

The vendored [doctest header](third_party/doctest/doctest.h) is unmodified.
SHA-256: `94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced`.

Python runs optional integration tests and release tools. Clang libFuzzer, ASan and UBSan
are optional development tools. They are not bundled with ordinary library or CLI builds.

The optional preview-cache example links [OpenSSL](https://www.openssl.org/) for SHA-256,
[libpng](https://libpng.org/pub/png/libpng.html) and [libjpeg](https://libjpeg-turbo.org/)
for image decoding and PNG output. Its integration tests use Python Pillow.

The optional comparison links [SQLite](https://www.sqlite.org/copyright.html) and,
when enabled, [RocksDB](https://rocksdb.org/). These dependencies are supplied by the
build environment. None is vendored or linked into the ShutterDB library or ordinary CLI.
