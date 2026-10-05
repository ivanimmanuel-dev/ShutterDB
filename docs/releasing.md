# Reproducing an experimental release

Version metadata lives in `VERSION`, the public `shutter::version` string and CMake's
numeric project version. `tools/package-release.py` rejects disagreement. Format v1 is
independent of the library version. No stable or production-readiness promise is implied
by v0.1.0; GitHub releases are explicitly marked **prerelease / experimental**.

## Source archive

From the tagged Git checkout, using Python 3.10+ and Git:

```sh
git checkout v0.1.0
python3 tools/package-release.py --ref v0.1.0 --output ../release
```

The source ZIP uses only tracked Git objects at that commit, sorted paths, stored ZIP
entries, canonical Unix permissions and the commit timestamp. Working-tree changes,
untracked files, caches and build output cannot enter it. Repeating this command produces
the same source ZIP bytes across Linux and Windows. The JSON manifest records the commit,
epoch, file hashes and archive hashes. Verify `SHA256SUMS-source.txt` with `sha256sum -c`
or compare with PowerShell `Get-FileHash -Algorithm SHA256`.

Extract the source ZIP into a new directory, then run the README's three build commands.
No network access is required for the default source build. Python enables additional CLI
and fault tests; CMake, a compiler and ordinary system libraries are the only build needs.
Install to a clean prefix and run both independent consumer modes:

```sh
cmake --install build --config Release --prefix ../stage
python3 tools/test-consumer.py --prefix ../stage
python3 tools/test-consumer.py --source .
python3 tools/test-readme.py ../stage
python3 tools/demo.py build/shutter
```

On Visual Studio generators use `build/Release/shutter.exe` for the demo. The release
workflow builds Windows x64 MSVC and Linux x64 Ubuntu 24.04 binaries from clean checkouts.
They require compatible system runtimes and C++ ABIs. Binary builds are reproducible by
procedure, not promised byte-identical across compiler/runner revisions; use the source
archive to build for another environment. The standard CMake export is relocatable.

## Publishing gate

1. Resolve serious findings in [the blocker ledger](release-blockers.md). Review API,
   format, durability, changelog and release notes. Obtain maintainer authorization.
2. Run local stress, corruption, faults, fuzz and benchmarks. Record measured results and
   limitations. Complete hosted `CI` on the exact candidate commit.
3. Create the matching annotated tag only after every required gate passes. If meaningful
   durability or CI issues remain, use an alpha version and describe the unresolved issues.
4. Manually dispatch `Experimental release` with that tag. The workflow checks version
   consistency and successful CI on the exact commit, builds/tests fresh packages on both
   platforms, verifies checksums and publishes a GitHub prerelease with notes.

```sh
git tag -a v0.1.0 -m "ShutterDB v0.1.0 experimental"
git push origin v0.1.0
gh workflow run release.yml -f tag=v0.1.0
```

The workflow never publishes merely because a tag exists. Its `GITHUB_TOKEN` comes from
Actions; credentials are not stored in the repository. The publishing step intentionally
fails if the release already exists, so a retry cannot silently replace published artifacts.
