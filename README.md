# ShutterDB

[![CI](https://github.com/ivanimmanuel-dev/ShutterDB/actions/workflows/ci.yml/badge.svg)](https://github.com/ivanimmanuel-dev/ShutterDB/actions/workflows/ci.yml)
[![Release](https://img.shields.io/badge/release-v0.1.0-blue)](https://github.com/ivanimmanuel-dev/ShutterDB/releases/tag/v0.1.0)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

An embedded C++20 database for generated assets and disk caches.
Store binary data in a checksummed log, set a disk budget and reuse it across restarts.
The library and CLI have no third-party runtime dependencies.

```cpp
#include <shutter/cache.hpp>
#include <iostream>

int main() {
    shutter::Cache cache("assets.shdb", {.max_bytes = 256 * 1024 * 1024});
    cache.put("hello", "world");
    cache.sync();

    if (auto value = cache.get_string("hello")) {
        std::cout << *value << '\n';
    }
}
```

- **Bounded caches** with least-recently-used eviction and automatic compaction.
- **Batched writes** with explicit `sync()`, or synchronization on each write.
- **CRC32C checksums** on file headers, record headers, keys and values.
- **Values on disk**, with keys and record locations held in memory.
- **Recovery and compaction**, plus a CLI for inspection and binary I/O.
- **One CMake target** for the cache and the ordinary key-value database.

Use [`Cache`](docs/cache.md) for data you can regenerate, or [`DB`](docs/api.md) to retain
records until explicitly deleted. Cache writes are buffered by default; DB writes are
synchronized by default. CI covers Linux, Windows and macOS. Each file has one owning
handle, with serialized calls. See [durability](docs/durability.md) for recovery semantics.

## Install

Build the current v0.2 development version with CMake 3.21+ and a C++20 compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The CLI is `build/shutter`, or `build/Release/shutter.exe` with Visual Studio.
Python 3 enables the CLI and fault-injection test suites.
The [v0.1.0 packages](https://github.com/ivanimmanuel-dev/ShutterDB/releases/tag/v0.1.0)
contain the original DB API and predate the bounded cache and image example.

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

## Asset caches

Use content hashes as keys to store previews, compiled shaders or generated assets.
The [preview-cache example](docs/asset-cache.md) reads PNG/JPEG images, writes PNG
previews with transparency, shares entries between identical files and bounds its cache.

The [RocksDB and SQLite comparison](docs/asset-cache-v02-results.md) measures 64 MiB
and 1 GiB datasets, repeated updates, reopen time and compaction. It includes raw results,
tradeoffs and a runnable harness. Paired local tests measured **1.22–3.34× faster reopening**
than the previous engine. The report records the shared-host conditions behind those numbers.

For a Roblox project, ShutterDB can run in development tools or behind an external API.
The [Roblox guide](docs/roblox.md) explains the integration and which data belongs in
Roblox DataStores.

## Documentation

| Guide | Contents |
|---|---|
| [Getting started](docs/getting-started.md) | Build, install, vendor and link |
| [C++ API](docs/api.md) | Operations, options, ownership and limits |
| [Bounded cache](docs/cache.md) · [Image example](docs/asset-cache.md) | Disk budgets, eviction and PNG/JPEG previews |
| [CLI](docs/cli.md) | Commands and examples |
| [Durability](docs/durability.md) | Synchronization, recovery and backups |
| [Errors](docs/error-handling.md) | Error codes and verification reports |
| [Architecture](docs/architecture.md) · [File format](docs/file-format.md) | Storage internals and format v1 |
| [Testing](docs/testing.md) · [Release results](docs/validation.md) | Test commands and measured coverage |
| [Benchmarks](docs/benchmarks.md) | Workloads, methodology and results |

[Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [Changelog](CHANGELOG.md) · [MIT license](LICENSE)
