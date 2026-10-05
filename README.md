# ShutterDB

[![CI](https://github.com/ivanimmanuel-dev/ShutterDB/actions/workflows/ci.yml/badge.svg)](https://github.com/ivanimmanuel-dev/ShutterDB/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ivanimmanuel-dev/ShutterDB?include_prereleases)](https://github.com/ivanimmanuel-dev/ShutterDB/releases)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

Persistent binary caches for C++20.

Keep generated assets between runs, set a disk budget and reclaim obsolete records
automatically. ShutterDB stores values on disk and indexes keys in memory. It builds
as a static library with no dependencies beyond the C++ standard library and OS APIs.

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

- LRU eviction and automatic compaction keep the main cache file within its budget.
- Batched writes share a `sync()`; individual writes can synchronize on each call.
- CRC32C checks validate headers, keys and values during reads and recovery.
- The `shutter` CLI inspects, verifies, compacts and repairs incomplete tails.

| API | Retention | Default writes |
|---|---|---|
| [`Cache`](docs/cache.md) | Evicts entries to meet a disk budget | Buffered; call `sync()` at checkpoints |
| [`DB`](docs/api.md) | Retains records until explicitly deleted | Synchronized on each write |

Each file has one owning handle; calls on that handle are serialized. Compaction uses
temporary space beyond the cache budget. See [cache behavior](docs/cache.md) and
[durability](docs/durability.md) for the storage contract.

## Install

Build from source with CMake 3.21+ and a C++20 compiler:

```sh
git clone https://github.com/ivanimmanuel-dev/ShutterDB.git
cd ShutterDB
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The CLI is `build/shutter`, or `build/Release/shutter.exe` with Visual Studio.
The [v0.2.0 release](https://github.com/ivanimmanuel-dev/ShutterDB/releases/tag/v0.2.0)
includes a source archive and Linux/Windows packages with the CLI, library, headers
and CMake configuration.

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

Use content hashes as keys for previews, compiled shaders and build artifacts. The
[image example](docs/asset-cache.md) turns PNG/JPEG sources into cached PNG previews,
preserves transparency and reuses identical content across filenames and restarts.

The [SQLite and RocksDB comparison](docs/asset-cache-v02-results.md) measures batched
ingestion, reads, updates, reopening and compaction on 64 MiB and 1 GiB datasets.
It includes the workload, environment, raw results and commands to reproduce the runs.

## Documentation

| Guide | Contents |
|---|---|
| [Getting started](docs/getting-started.md) | Build, install, vendor and link |
| [C++ API](docs/api.md) | Operations, options, ownership and limits |
| [Cache API](docs/cache.md) · [Image example](docs/asset-cache.md) | Disk budgets, eviction and PNG/JPEG previews |
| [CLI](docs/cli.md) | Commands and examples |
| [Durability](docs/durability.md) | Synchronization, recovery and backups |
| [Errors](docs/error-handling.md) | Error codes and verification reports |
| [Architecture](docs/architecture.md) · [File format](docs/file-format.md) | Storage internals and format v1 |
| [Testing](docs/testing.md) | Test suites, sanitizers, fuzzing and package checks |
| [Benchmarks](docs/benchmarks.md) | Workloads, methodology and results |

[Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [Changelog](CHANGELOG.md) · [MIT license](LICENSE)
