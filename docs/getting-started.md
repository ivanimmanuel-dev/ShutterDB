# Getting started

ShutterDB requires CMake 3.21+, a C++20 compiler and a local filesystem.
Source builds include the test dependency and require no downloads.

## Build and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix /your/install/prefix
```

Python 3 enables CLI, corruption and Linux syscall tests. The C++ tests run without Python.
The install contains a static library, public headers, CMake package files and the `shutter` CLI.

Add the package to your application's `CMakeLists.txt`:

```cmake
find_package(ShutterDB CONFIG REQUIRED)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE ShutterDB::ShutterDB)
```

Configure with `-DCMAKE_PREFIX_PATH=/your/install/prefix`. The imported target supplies
the include directory, C++20 requirement and platform thread library.

Release binary packages are ready-made installations: use the extracted directory as
the prefix. Windows packages require the Microsoft Visual C++ runtime; Linux packages
target Ubuntu 24.04 x64. Build from source for other compiler or runtime combinations.

## Vendoring

With the source at `vendor/shutterdb`:

```cmake
add_subdirectory(vendor/shutterdb)
target_link_libraries(myapp PRIVATE ShutterDB::ShutterDB)
```

Or use local FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(ShutterDB SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/shutterdb")
FetchContent_MakeAvailable(ShutterDB)
target_link_libraries(myapp PRIVATE ShutterDB::ShutterDB)
```

Tests, CLI and benchmarks are off by default when ShutterDB is a subproject.

## Check the installation

These commands create, build and run separate consumer projects in temporary directories:

```sh
python3 tools/test-consumer.py --prefix /your/install/prefix
python3 tools/test-consumer.py --source .
```

See the [C++ API](api.md) for storage operations and [CLI reference](cli.md) for the executable.
For automatically managed asset storage, see the [bounded cache API](cache.md).
