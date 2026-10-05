# Getting started

Requirements: CMake 3.21+, GCC/Clang/MSVC with C++20, and a local filesystem. Build and test as shown in the README. No network access is needed to build the bundled source tree.

## Installed package

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$PWD/install"
cmake -S examples/consumer -B consumer-build -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build consumer-build --config Release
ctest --test-dir consumer-build -C Release --output-on-failure
```

Use a path appropriate to your shell on Windows. The imported target is `ShutterDB::ShutterDB`; it supplies include paths, C++20 and the platform thread library. The current build exports a static library.

## Vendoring or FetchContent

```cmake
add_subdirectory(vendor/shutterdb)
target_link_libraries(myapp PRIVATE ShutterDB::ShutterDB)
```

Or, for an already downloaded checkout:

```cmake
include(FetchContent)
FetchContent_Declare(ShutterDB SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/shutterdb")
FetchContent_MakeAvailable(ShutterDB)
target_link_libraries(myapp PRIVATE ShutterDB::ShutterDB)
```

Tests, CLI and benchmarks are off by default when included as a subproject.
Both the installed package and local `FetchContent` workflows are tested in
independent temporary projects with binary values and a real process restart:

```sh
python3 tools/test-consumer.py --prefix /your/install/prefix
python3 tools/test-consumer.py --source .
```

The upstream repository is [ivanimmanuel-dev/ShutterDB](https://github.com/ivanimmanuel-dev/ShutterDB).
Pin a reviewed commit when fetching from Git. vcpkg, Conan, Homebrew and AUR publication remain future work.

## API essentials

`DB(path)` creates a missing database and opens an existing one. Parent directories must exist. A zero-length existing file is corruption, not an empty database. The object is intentionally noncopyable and nonmovable, and must outlive all operations using it.

```cpp
shutter::Options opts;
opts.create_if_missing = false;
opts.recover_truncated_tail = false; // Fail rather than repair an incomplete tail.
shutter::DB db("app.shdb", opts);

const std::array bytes{std::byte{0}, std::byte{255}};
db.put("binary", bytes);
auto value = db.get("binary");       // optional<vector<byte>>
auto text = db.get_string("binary"); // optional<string>; preserves every byte
bool present = db.contains("binary");
bool removed = db.remove("binary");
auto stats = db.stats();
auto report = db.verify();
db.compact();
```

Include `<array>` for the binary example. Keys are arbitrary byte strings of 1–65,536 bytes; values contain 0–16,777,216 bytes. String helpers do not interpret encoding or remove NULs. `contains` consults the index; `get` additionally validates the current record on disk.

The default index budget is one million live keys and 256 MiB of accounted memory. Each entry is charged key length plus 128 bytes; this is a conservative accounting policy, not an exact allocator/RSS cap. Set `max_live_keys` and `max_index_bytes` explicitly for larger databases, and provision memory accordingly.

Keep the database and its `.lock`/recovery sidecars together. Do not delete the lock file while any handle is open. Use `--` before CLI keys or values starting with `--`.

`put` copies its input during the call; callers need not retain the input buffers afterward.
`get`, `get_string`, `stats` and `verify` return owned values, with no borrowed storage or
iterator lifetime to manage. Errors use `shutter::Error` with a stable category and optional
checksum diagnostics; allocation failures may propagate as standard exceptions.
Recovery is an open policy in `Options` and an explicit CLI command; `inspect` and `verify`
never repair database bytes. `inspect` may create the persistent lock sidecar.
Pre-1.0 API evolution is possible; rebuild consumers with the matching headers and library.
