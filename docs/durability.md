# Durability contract

ShutterDB v0.1.0 is experimental. The implementation uses persistence primitives and has deterministic process-crash tests; this is not proof of power-loss safety on every device. Linux is the reference platform. Keep recoverable source data and backups while evaluating it.

| Mode | A successful `put`/`remove` means | Risk |
|---|---|---|
| Default, `sync_writes=true` | All record bytes have reached OS write calls and a file synchronization call returned successfully before the index is changed | Depends on correct filesystem, OS, device and power-loss behavior |
| Buffered, `sync_writes=false` | All record bytes have reached the kernel; no durability barrier has been requested for this write | OS crash/power loss may discard or tear recent writes |
| `db.sync()` | An explicit synchronization call has completed for the current log | Earlier buffered writes are only as durable as the platform's sync contract |

## Buffers and barriers

There is no userspace stream buffer in the storage layer. POSIX uses `pwrite`/`pread`, `ftruncate` and `fsync`; new-file publication and compaction synchronize the parent directory after namespace changes. Windows uses `WriteFile`/`ReadFile`, `SetEndOfFile`, `FlushFileBuffers`, and replacement with `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. Windows has no identical portable directory-fsync contract in this implementation. macOS builds use POSIX `fsync`, not Apple's stronger `F_FULLFSYNC`; do not infer stronger hardware persistence guarantees.

There is no timer-based flush and the destructor does not report or promise a durability barrier. Call `sync()` explicitly before relying on buffered data. Durability is a handle option, not a format field; opening a file does not reveal how historical writes were synchronized.

## Crashes and uncertain writes

A process crash after a successful synchronous operation should retain that operation if the platform honors synchronization. An interrupted append can leave a partial final record. Opening normally validates the entire prefix, truncates only a recognized incomplete tail and syncs the truncation. A full-sized final record with a bad checksum is rejected, not discarded. Applications may disable tail recovery and inspect before deciding to repair.

If a write or flush throws, that operation may be absent or present after reopening. If an exception occurs after the OS sync, it may already be durable. The handle is latched into `NEEDS_REOPEN` and cannot be reused. A failed `compact` may leave the original physical file or the new compacted file; the logical key/value contents are the same. The durable backup supports recovery if replacement is incomplete. See [architecture](architecture.md).

## Assumptions and exclusions

- Local filesystems with working file locks, synchronization and atomic rename semantics on POSIX.
- No rogue writer, directory rename, sidecar removal, hardlink manipulation or file editing while open.
- Hardware that honors flush requests, and no damage to previously synchronized sectors from a later torn write (the usual powersafe-overwrite assumption).
- No protection against disk destruction, controller firmware bugs, undetected CRC collisions, malicious edits or whole-record suffix deletion.
- No multi-key atomicity or transaction rollback. Individual record validation is not a transaction system.
- No guarantee that corruption is always automatically recoverable. Failing closed is deliberate.

The crash suite terminates processes at eleven deterministic boundaries, without destructors.
Linux syscall tests additionally exercise short I/O, EINTR, ENOSPC, failed writes, reads,
truncation, file/directory sync and replacement, including initial publication. A failed sync
can leave a complete visible record: an error does not promise rollback.

Hosted Linux and Windows CI has passed; see [validation](validation.md). These tests do not
cut power, reboot the kernel, emulate sector tearing or a reordered block device, or inject
every low-level error. Physical power-cut and broader filesystem/device campaigns remain
future validation. No "crash proof" claim is made.
