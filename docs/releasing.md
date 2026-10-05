# Releasing

## Prepare a version

Update `VERSION`, `shutter::version` in `include/shutter/db.hpp`, CMake's numeric project
version, the changelog and release notes. The on-disk format version is separate.
`tools/package-release.py` checks version consistency.

Run the [test suites](testing.md), stress workload, fuzz campaigns and benchmarks.
Record results in [validation](validation.md). The candidate commit must pass hosted CI.

## Package source

With Python 3.10+ and Git, from a tagged checkout:

```sh
python3 tools/package-release.py --ref v0.1.0 --output ../release
```

The source ZIP contains tracked Git objects, sorted paths, stored entries, file permissions
and the commit timestamp. This produces identical source ZIP bytes on Linux and Windows.
Manifests record the commit, file hashes and archive hashes. Verify the accompanying sums
with `sha256sum -c SHA256SUMS-source.txt` or PowerShell `Get-FileHash -Algorithm SHA256`.

Extract the archive into a fresh directory and follow the README build instructions.
Then check the installation and examples:

```sh
cmake --install build --config Release --prefix ../stage
python3 tools/test-consumer.py --prefix ../stage
python3 tools/test-consumer.py --source .
python3 tools/test-readme.py ../stage
python3 tools/demo.py build/shutter
```

With Visual Studio, use `build/Release/shutter.exe` for the demo.

## Publish

After CI passes on the candidate commit, create an annotated tag and dispatch the release
workflow. For v0.1.0 these commands were:

```sh
git tag -a v0.1.0 -m "ShutterDB v0.1.0 experimental"
git push origin v0.1.0
gh workflow run release.yml -f tag=v0.1.0
```

The workflow checks the tag, version and exact-commit CI result. It builds and tests fresh
Linux and Windows x64 packages, verifies checksums and creates a GitHub prerelease.
It stops if the release already exists. Existing tags and published archives stay immutable.

Source archives are byte-reproducible. Binary output depends on the compiler and runner
revision; packages require compatible system runtimes and C++ ABIs.
