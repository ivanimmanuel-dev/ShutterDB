# ShutterDB

[![CI](https://github.com/ivanimmanuel-dev/ShutterDB/actions/workflows/ci.yml/badge.svg)](https://github.com/ivanimmanuel-dev/ShutterDB/actions/workflows/ci.yml)
[![Release](https://img.shields.io/badge/release-v0.1.0-blue)](https://github.com/ivanimmanuel-dev/ShutterDB/releases/tag/v0.1.0)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

Embedded key-value storage for C++20. ShutterDB stores binary keys and values in a
checksummed, append-only log. Link the library and open a file.

```cpp
#include <shutter/db.hpp>
#include <iostream>

int main() {
    shutter::DB db("app.shdb");
    db.put("hello", "world");

    if (auto value = db.get_string("hello")) {
        std::cout << *value << '\n';
    }
}
```

- **Synchronous writes by default**, with buffered writes and explicit `sync()` available.
- **CRC32C checksums** on file headers, record headers, keys and values.
- **Values on disk**, with keys and record locations held in memory.
- **Recovery and compaction**, plus a CLI for inspection and binary I/O.
- **One CMake target**, with no third-party runtime dependencies.

v0.1.0 is experimental. CI covers Linux, Windows and macOS. Each database has one owning
handle; calls on that handle are serialized. See [durability](docs/durability.md) for write
and recovery semantics.

## Install

Download [source, Linux x64 or Windows x64 packages](https://github.com/ivanimmanuel-dev/ShutterDB/releases/tag/v0.1.0),
or build from source with CMake 3.21+ and a C++20 compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The CLI is `build/shutter`, or `build/Release/shutter.exe` with Visual Studio.
Python 3 enables the CLI and fault-injection test suites.

## Use in your project

Install the library and headers:

```sh
cmake --install build --config Release --prefix /your/install/prefix
```

Link the exported target:

```cmake
find_package(ShutterDB CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE ShutterDB::ShutterDB)
```

Configure your project with `-DCMAKE_PREFIX_PATH=/your/install/prefix`.
[Vendoring and FetchContent](docs/getting-started.md#vendoring) are also supported.

## Command line

With `shutter` on your PATH:

```sh
shutter init app.shdb
shutter set hello world --db app.shdb
shutter get hello --db app.shdb
shutter verify --db app.shdb
shutter compact --db app.shdb
```

Use `--value-file` to store a binary file and `get --raw` to read it back.
The [CLI reference](docs/cli.md) covers commands, JSON output and exit codes.

## Documentation

| Guide | Contents |
|---|---|
| [Getting started](docs/getting-started.md) | Build, install, vendor and link |
| [C++ API](docs/api.md) | Operations, options, ownership and limits |
| [CLI](docs/cli.md) | Commands and examples |
| [Durability](docs/durability.md) | Synchronization, recovery and backups |
| [Errors](docs/error-handling.md) | Error codes and verification reports |
| [Architecture](docs/architecture.md) · [File format](docs/file-format.md) | Storage internals and format v1 |
| [Testing](docs/testing.md) · [Release results](docs/validation.md) | Test commands and measured coverage |
| [Benchmarks](docs/benchmarks.md) | Workloads, methodology and results |

[Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [Changelog](CHANGELOG.md) · [MIT license](LICENSE)
