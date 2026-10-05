# Security

Report vulnerabilities through [GitHub private reporting](https://github.com/ivanimmanuel-dev/ShutterDB/security/advisories/new).
Include the version or commit, compiler, OS, filesystem, reproduction steps and relevant
diagnostics. Use a minimal synthetic database rather than private application data.

Memory-safety defects, unbounded parsing, broken process locking and silent data loss
are security-relevant. Reports should identify the input or operation that triggers the failure.

ShutterDB is an embedded library for local files in a trusted directory. The application
controls filesystem access. CRC32C detects accidental corruption and does not authenticate
data; ShutterDB does not provide encryption or access control. The storage assumptions
are documented in [durability](docs/durability.md).
