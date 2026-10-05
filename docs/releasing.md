# Release checklist

Version the API with semantic versions; keep the on-disk format version independent. Current status is `0.1.0-dev`, not a published v0.1 release.

1. Review every item in [roadmap](roadmap.md) and the local [validation report](validation.md). Resolve or explicitly document release-blocking issues.
2. Update `project(... VERSION ...)`, the public `version` string and CHANGELOG together. Do not tag while the public string still says `-dev`.
3. Run the full hosted matrix on the exact candidate commit: Linux GCC/Clang Debug/Release, ASan, UBSan, Windows, macOS, formatting, analysis and consumers.
4. Run longer fuzz/failure campaigns and record corpus/reproducer results. Have another maintainer review the durability contract and replacement protocol.
5. Build a source archive including doctest and its license. Extract it into a clean directory, build offline, run the README example and install/consume the package.
6. Generate SHA-256 sums and attach the source archive, release notes, compatibility notes and measured validation evidence.
7. Create the tag/release only after human maintainer approval. This repository does not automatically publish on a tag or carry deployment credentials.

vcpkg, Conan, Homebrew and AUR recipes must first be validated in their actual package-manager workflows. None is advertised as published by this preview. A future format change needs an explicit migration strategy; opening an unknown version must continue to fail safely.
