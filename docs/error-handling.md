# Errors and inspection

Missing keys return an empty optional. Failures throw `shutter::Error`, derived from
`std::runtime_error`, with a category, byte offset and optional CRC32C values.
Allocation and filesystem path conversion can also throw standard exceptions.

```cpp
try {
    shutter::DB db("app.shdb");
    db.put("hello", "world");
} catch (const shutter::Error& error) {
    std::cerr << shutter::error_name(error.code()) << ": " << error.what()
              << " at byte " << error.offset() << '\n';
}
```

| Category | Meaning and action |
|---|---|
| `INVALID_ARGUMENT` | Check key/value limits, paths or CLI syntax. |
| `NOT_FOUND` | The database is absent and creation is disabled. |
| `IO_ERROR` | Check storage availability. After a failed write, close and reopen. |
| `PERMISSION_DENIED` | Check file and directory permissions. |
| `CORRUPTION` | Preserve a copy and inspect the reported offset. |
| `UNSUPPORTED_FORMAT` | Open the file with a compatible version. |
| `LOCK_CONFLICT` | Close the other owning handle or wait for its process to exit. |
| `RESOURCE_LIMIT` | An index budget, file-offset limit or sequence limit was reached. |
| `NEEDS_REOPEN` | Destroy the failed handle and reopen to resolve its on-disk state. |

A failed write may be present after reopening. Read the affected key before retrying;
there is no transaction rollback. See [durability](durability.md#failed-operations).

## Verification reports

`DB::inspect(path)` scans database bytes read-only under a shared sidecar lock.
`db.verify()` scans through an open handle. Both return `VerifyReport` for format and
record errors; opening and locking failures throw. Inspection may create the lock sidecar.

Reports include header validity, file size, valid-prefix length, record counts, live keys,
reclaimable bytes, an incomplete-tail flag and the first diagnostic. CRC diagnostics
include expected and computed values. Counts describe the scanned prefix.

`report.ok()` requires a fully valid file. An incomplete tail returns false even when it
can be recovered. To repair a copy of a closed database:

```sh
shutter recover --db copy.shdb
shutter verify --db copy.shdb
```

Recovery truncates recognized incomplete tails. It rejects complete records with invalid
checksums. See the [CLI reference](cli.md) for JSON output and exit codes.
