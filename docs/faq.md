# FAQ

**Should I replace SQLite or RocksDB with this?** Not on the strength of a new preview. Evaluate ShutterDB for simple, modest-sized C++ key/value workloads. Existing engines have years of operational experience and capabilities this project deliberately lacks.

**Does the entire database live in memory?** No. Owned keys and record metadata do; values are read from the log. Memory grows with the number and size of live keys. Startup scans the whole log.

**Does it support binary values and keys?** Yes, including NUL bytes and empty values. Empty keys are invalid. Keys are case-sensitive byte sequences; no encoding normalization occurs.

**Can several processes open it?** One owning DB process/handle at a time. Inspectors may share a lock with other inspectors, but they reject an active writer. There is no concurrent writer/read-only DB API yet.

**Can threads share one DB?** Calls serialize through a single mutex. Object lifetime must be externally coordinated. No asynchronous operations or parallel reads are claimed.

**Is it crash-safe?** It is designed around append, checksum verification, explicit synchronization and recovery. Tests cover process termination. See the precise [durability contract](durability.md); this preview is not certified against physical power failures.

**Why not recover a last record with a bad checksum?** A full record with damaged data could be corruption, not an interrupted write. Silently discarding it risks concealing loss of acknowledged data. Partial records have a separately documented recovery policy.

**Is deletion secure erasure?** No. Tombstones remove logical visibility. Old values remain in the log until compaction and can remain in filesystem/device history afterward. There is no encryption or secure wipe feature.

**Why does a `.lock` file remain?** The pathname must stay stable across compaction and process exits. It is not evidence that a process still holds the OS lock. Never remove it while the database is open.

**How do I make a backup?** Close all handles, then copy the database file. Do not copy an actively written file or a set of interrupted-compaction sidecars and assume the copy is consistent. No online backup API exists yet.

**Can I use a network share, OneDrive or mixed WSL/Windows access?** Not as a supported storage configuration. Use a local directory and one OS environment; synchronization, aliasing and locking assumptions may differ on remote/synchronized mounts.

**Why exceptions instead of Result?** C++20 has no standard `expected`; exceptions preserve the small API. Missing values use `optional`. `result.hpp` contains the typed exception contract; it does not define a misleading custom Result abstraction.

**Why no Google Benchmark dependency?** Coarse, machine-readable end-to-end timings meet the initial need without another dependency. More rigorous latency/distribution analysis can be added when measurements justify it.
