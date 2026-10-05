# Durability

## Write modes

| Mode | Successful write | Synchronization |
|---|---|---|
| `sync_writes=true` (DB default) | Record written and synchronized before updating the index | Each write |
| `sync_writes=false` | Record written to the kernel before updating the index | Explicit `sync()` |

`sync()` synchronizes the current log. Buffered writes since the last successful sync
may be lost or torn during OS or power failure. Closing a handle does not sync it.
The write mode belongs to the handle and is not stored in the file. `Cache` defaults
to buffered writes; its `storage.sync_writes` option enables per-write synchronization.

## Platform operations

| Platform | File synchronization | Replacement |
|---|---|---|
| Linux | `fsync` | Atomic rename followed by parent-directory `fsync` |
| macOS | `fsync` | Rename followed by parent-directory `fsync` |
| Windows | `FlushFileBuffers` | `MoveFileExW` with replacement and write-through flags |

macOS builds do not request `F_FULLFSYNC`. Windows has no equivalent directory-fsync
operation in this implementation. Persistence depends on the filesystem and device
honoring these operations.

## Recovery

Opening a database validates the file header and every record. An incomplete final record
is truncated and the truncation synchronized. Set `recover_truncated_tail=false` to reject
the file instead. A complete record with a bad checksum is corruption, including at EOF.

An incomplete header cannot be fully checksummed. Lost suffix bytes and an interrupted
append can look identical; deletion at a record boundary is undetectable without external
history. The exact checks are specified in [format v1](file-format.md#recovery-rules).

Compaction verifies and syncs a replacement and a backup before replacing the original.
On reopen, a valid primary takes precedence. If it is absent or corrupt, a valid backup
is restored. An unsupported version or an I/O/resource error prevents fallback.
See the [compaction protocol](architecture.md#compaction-protocol).

## Failed operations

A failed write or sync may leave its record present after reopening. ShutterDB marks an
uncertain handle `NEEDS_REOPEN`; close it, reopen and read the affected key before retrying.
A failed compaction can leave either physical log, with the same logical contents.
Operations are per key; there is no multi-key transaction or rollback.

## Filesystem requirements

Use a local filesystem with working locks, synchronization and rename semantics. Keep the
database and its sidecars in a trusted directory. Leave the lock file in place while any
handle is open, and coordinate external file operations with database ownership.
Network filesystems, cloud-synchronized directories and mixed Windows/WSL access to the
same open database are unsupported.

The write protocol assumes a later torn write does not damage previously synchronized
sectors. Physical power cuts, sector tearing and reordered block writes have not been
tested. Executed process-exit, syscall-failure and corruption tests are recorded in
[release results](validation.md).

## Backups and deletion

Close all handles before copying the database. After an interrupted compaction, reopen
and close it first to resolve sidecars, then copy the primary file. Online backup is not
supported.

Deleting a key appends a tombstone. Compaction removes old values from the current log;
filesystem snapshots and device history may retain them. CRC32C detects accidental
corruption; it provides neither encryption nor authentication.
