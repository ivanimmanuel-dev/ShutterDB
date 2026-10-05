# Error handling and inspection

The error model is exceptions for failures and optional values for absent keys. `shutter::Error` derives from `std::runtime_error`. It carries `code()`, `offset()` and optional expected/actual CRC32C values. Standard allocation failures may still throw `std::bad_alloc`. Filesystem path conversion can throw a standard filesystem exception before a file is opened.

```cpp
try {
    shutter::DB db("app.shdb");
    db.put("hello", "world");
} catch (const shutter::Error& error) {
    std::cerr << shutter::error_name(error.code()) << ": " << error.what()
              << " at byte " << error.offset() << '\n';
}
```

| Code | Action |
|---|---|
| `INVALID_ARGUMENT` | Check key/value limits, empty paths and CLI syntax. |
| `NOT_FOUND` | Database file is absent and creation is disabled. A missing key is an empty optional, not an exception. |
| `IO_ERROR` | Check storage availability; write outcome may be uncertain. Close and reopen. |
| `PERMISSION_DENIED` | Check file and parent-directory permissions. |
| `CORRUPTION` | Preserve a copy and inspect it. Do not treat it as an absent key. |
| `UNSUPPORTED_FORMAT` | Use a compatible reader; do not modify the file with this version. |
| `LOCK_CONFLICT` | Another handle/process owns the database. Close it; never delete its lock sidecar. |
| `RESOURCE_LIMIT` | Key index budget, file-offset limit or sequence space exhausted. Increase only the relevant configurable budget. |
| `NEEDS_REOPEN` | A failed write/compaction latched the handle. Destroy it and reopen to resolve the on-disk state. |

An unsuccessful write can still appear after reopen. Build retry logic around idempotent key assignments and inspect the resulting value. There is no multi-operation rollback or transaction boundary.

## Reports without repair

`DB::inspect(path)` opens database data read-only under a shared sidecar lock. It returns `VerifyReport` for format/record failures. Opening or locking failures throw. `db.verify()` checks an already-open database with the same scanner. Neither modifies database bytes.

The report includes file-header validity, format, database size, complete-prefix record counts, PUTs, DELETEs, live keys, live record bytes, reclaimable bytes, valid-prefix length, an incomplete-tail flag and the first diagnostic. CRC failures include expected and computed checksums. Scanning intentionally stops at the first error rather than guessing where later records begin.

`report.ok()` is true only for a completely validated file. A recoverable partial tail is **not** an OK report. Before repair, copy the closed database. Then use `shutter recover --db copy.shdb` for incomplete tails. This command cannot repair arbitrary corruption and will refuse a full-sized invalid record.

Do not run diagnostic readers concurrently with external file editors. The lock protects cooperating ShutterDB users, not programs that ignore locks.
