# Dependencies and provenance

The library and CLI require only C++20 and OS APIs. CMake's Threads target represents the platform thread support, not an added database runtime.

| Component | Version | Use | License |
|---|---|---|---|
| doctest | 2.4.12 | Vendored test assertions and runner only | MIT, `third_party/doctest/LICENSE.txt` |
| Python | 3.x, optional | CLI integration tests and developer scripts | Supplied by the developer, not redistributed |
| Clang libFuzzer, ASan, UBSan | Toolchain supplied | Optional parser campaigns and development diagnostics | Not linked into ordinary release builds |

doctest was obtained unchanged from the [upstream v2.4.12 header](https://github.com/doctest/doctest/blob/v2.4.12/doctest/doctest.h). SHA-256: `94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced`. Source engines consulted in the competitive audit were not vendored or copied.
