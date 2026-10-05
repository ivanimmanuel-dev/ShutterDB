# Releasing

## Prepare

Update `VERSION`, `shutter::version` in `include/shutter/db.hpp`, CMake's project
version, `CHANGELOG.md` and `docs/releases/vVERSION.md`. The on-disk format version
is independent of the package version.

```sh
python3 tools/check-project.py
python3 tests/test_release.py
```

Run the [test suites](testing.md) for the changed components and commit the candidate.
Storage changes also need sanitizer, failure, stress and interoperability coverage;
parser changes need fuzzing. Performance claims need retained benchmark results.
The candidate commit must pass the complete hosted CI workflow.

## Check the packages

Package the committed candidate with Python 3.10+ and Git:

```sh
python3 tools/package-release.py --ref HEAD --output ../release
```

The source ZIP contains tracked Git objects, sorted paths, stored entries, file permissions
and the commit timestamp. This produces identical source ZIP bytes on Linux and Windows.
Manifests record the commit, file hashes and archive hashes. Verify the accompanying sums
with `sha256sum -c SHA256SUMS-source.txt` or PowerShell `Get-FileHash -Algorithm SHA256`.

Extract into a fresh directory, build and run CTest, then test the installed package:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix ../stage
python3 tools/test-consumer.py --prefix ../stage
python3 tools/test-consumer.py --source .
python3 tools/test-readme.py ../stage
python3 tools/demo.py build/shutter
```

With Visual Studio, use `build/Release/shutter.exe` for the demo.

Binary packaging adds `--install STAGE --platform linux-x64` or `windows-x64`, plus
`--runtime "OS, compiler and runtime requirements"`. Use a fresh installation from the
same commit. The packager checks the CLI version and records the runtime description.

## Publish

After CI passes on the candidate commit, create an annotated tag and dispatch the release
workflow. For v0.2.0:

```sh
git tag -a v0.2.0 -m "ShutterDB v0.2.0"
git push origin v0.2.0
gh workflow run release.yml -f tag=v0.2.0
```

Add `-f prerelease=true` for a prerelease. The workflow checks the tag, version and
exact-commit CI result. It builds and tests fresh Linux and Windows x64 packages,
verifies checksums and publishes the archives with the version's release notes.
Use a new version for changes to a published release.

Source archives are byte-reproducible. Binary output depends on the compiler and runner
revision; packages require compatible system runtimes and C++ ABIs.
