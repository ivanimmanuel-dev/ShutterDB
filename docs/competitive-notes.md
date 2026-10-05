# A focused competitive audit

Reviewed 2026-10-05 using upstream documentation. This is a design audit, not a performance comparison or a survey of issue frequency. No implementation code from another engine was copied. The friction assessments below are our interpretation of documented interfaces and caveats.

| Engine | Integration and API | Durability defaults and adoption tradeoffs |
|---|---|---|
| LevelDB | C++ key/value API with options and status objects. CMake; upstream clone instructions include submodules. Relatively compact public interface. | Ordinary writes are asynchronous unless `WriteOptions::sync` is enabled. One process per database; ordered iteration and batching are already mature. Our opportunity is a smaller basic API and sync defaults, not superior functionality. |
| RocksDB | C++ API, CMake/Make, optional compression and allocator libraries, extensive tuning and build choices. | WAL-backed writes normally do not force stable storage unless sync is requested. Rich options, write stalls and tuning responsibilities are reasonable costs of a much more capable engine. ShutterDB deliberately avoids that configuration surface. |
| LMDB | Small C library, packaged by many systems; environment/transaction/database handles and mapped values. | Durable transactions by default; flags such as `MDB_NOSYNC` weaken guarantees. Map sizing, transaction/value lifetimes, stale reader handling and filesystem restrictions require care. Excellent when those semantics fit. |
| SQLite | Straightforward C amalgamation or system package. Statements, bindings and transactions; SQL adds concepts for a key/value-only use case. | Rollback journaling defaults to full synchronization; WAL with FULL and NORMAL differ. SQLite's detailed failure model sets a high bar. ShutterDB only simplifies the key/value path; it does not match SQLite's maturity or transactions. |
| redb | Rust crate with typed table definitions and explicit transactions. Native C++ adoption entails a language boundary. | Immediate durability is the default. Strong ergonomics and crash-safety goals, with recovery and memory-mapped/tree architecture tradeoffs. Inspiration for clear defaults, not a reason to port a second runtime. |
| sled | Rust crate with a map-like API and explicit `flush`. | Upstream README documents periodic synchronization at 500 ms and warns that main is undergoing a rewrite. Version/documentation alignment and persistence boundaries are adoption considerations. Do not treat the README as evidence of a current stable implementation. |
| libmdbx | C/C++ interfaces, managed transaction and cursor abstractions, CMake/Make integration. | Its C++ documentation describes robust synchronous durability as default and offers weaker modes with explicit caveats. Environment, table, cursor and transaction concepts buy capabilities beyond this project's scope. Current official docs prominently support the C++ API. |
| Speedb | A RocksDB-compatible C++ engine with CMake targets and upstream build-dependency instructions. | Retains the RocksDB-style API and durability configuration surface. A useful alternative for applications already committed to that ecosystem. No maintenance or performance ranking was inferred from its marketing claims. |

Sources: [LevelDB usage](https://github.com/google/leveldb/blob/main/doc/index.md), [LevelDB build](https://github.com/google/leveldb/blob/main/README.md), [RocksDB basics](https://github.com/facebook/rocksdb/wiki/Basic-Operations), [RocksDB installation](https://github.com/facebook/rocksdb/blob/main/INSTALL.md), [LMDB API documentation](https://raw.githubusercontent.com/LMDB/lmdb/mdb.master/libraries/liblmdb/lmdb.h), [SQLite atomic commit](https://www.sqlite.org/atomiccommit.html), [SQLite WAL](https://www.sqlite.org/wal.html), [redb](https://github.com/cberner/redb), [redb write transaction](https://docs.rs/redb/latest/redb/struct.WriteTransaction.html), [sled](https://github.com/spacejam/sled), [libmdbx C++ core](https://libmdbx.dqdkfa.ru/doxygen/group__cxx__core.html), [Speedb](https://github.com/speedb-io/speedb).

## Decisions carried into the implementation

1. Synchronous writes are the default; buffering requires an explicit choice.
2. A RAII database object owns its file and lock. Missing keys use `optional`; failures throw typed errors.
3. There is one public CMake target and no external runtime library beyond the OS and C++ standard library.
4. The on-disk log is the authoritative state. An index is rebuilt from verified records at open.
5. Verification is useful without successful recovery, and never changes database bytes.
6. Manual compaction, bounded records and an explicit memory budget keep the design reviewable.

What we genuinely simplify: adding a modest amount of durable local key/value state to an existing C++20 application. What we do not simplify: datasets whose keys exceed RAM, high-throughput sync writes, transactional updates or concurrent processes. Choose a mature existing database for those needs.
