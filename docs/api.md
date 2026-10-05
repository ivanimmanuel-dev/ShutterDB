# C++ API

Include `<shutter/db.hpp>` and link `ShutterDB::ShutterDB`.

For regenerable data with eviction and a disk budget, use [Cache](cache.md).

## Open a database

```cpp
shutter::DB db("app.shdb");
```

The constructor creates a missing database. Its parent directory must exist.
Opening an existing file validates the log, rebuilds the index and recovers incomplete
final records by default. Empty or malformed files produce an error.

```cpp
shutter::Options options;
options.create_if_missing = false;
options.recover_truncated_tail = false;
shutter::DB db("app.shdb", options);
```

`DB` owns its file and process lock. It is noncopyable and nonmovable. Calls on one handle
are serialized by a mutex; its lifetime must cover all callers. A second owning handle
for the same database receives `LOCK_CONFLICT`.

## Read and write

```cpp
db.put("name", "Ada");
auto name = db.get_string("name"); // std::optional<std::string>
bool present = db.contains("name");
bool removed = db.remove("name");
```

Missing keys return an empty optional. `remove` returns false when the key is absent
and appends no record. `contains` checks the index; `get` and `get_string` also validate
the stored record and its checksum.

Keys and values are byte sequences. String helpers preserve NUL bytes and perform no
encoding conversion. For binary data:

```cpp
#include <array>

const std::array bytes{std::byte{0}, std::byte{255}};
db.put("binary", bytes);
auto value = db.get("binary"); // std::optional<std::vector<std::byte>>
```

`put` accepts `std::string_view` or `std::span<const std::byte>` and copies the input
during the call. Returned strings, vectors, statistics and reports own their data.

## Synchronization and maintenance

| Operation | Behavior |
|---|---|
| `sync()` | Synchronizes the current log with the OS |
| `stats()` | Returns cached key, record, byte and sequence counts |
| `compact()` | Rewrites live records, verifies the output and replaces the log |
| `verify()` | Scans an open database and returns a `VerifyReport` |
| `DB::inspect(path)` | Scans database bytes read-only under a shared lock |

`inspect` can create a `.lock` sidecar. It rejects an active owning handle.
Neither inspection method repairs the database. A verification report is OK only when
the entire file validates, including its final record.

Buffered mode defers synchronization until `sync()`:

```cpp
shutter::Options options;
options.sync_writes = false;
shutter::DB db("cache.shdb", options);
db.put("key", "value");
db.sync();
```

The destructor closes the handle without synchronizing buffered writes.
See [durability](durability.md) for failed-write and recovery behavior.

## Options and limits

| Option | Default | Purpose |
|---|---:|---|
| `sync_writes` | `true` | Synchronize each successful write |
| `create_if_missing` | `true` | Create a missing database |
| `recover_truncated_tail` | `true` | Truncate an incomplete final record on open |
| `max_live_keys` | `1,000,000` | Limit the number of indexed keys |
| `max_index_bytes` | `268,435,456` | Limit accounted index memory |

Keys contain 1–65,536 bytes; values contain 0–16,777,216 bytes. Each index entry is
charged key length plus 128 bytes. Allocator overhead and temporary record buffers
also contribute to process memory.

Database errors use `shutter::Error` with a category, offset and optional checksum values.
See [error handling](error-handling.md) for diagnostics and recovery.
