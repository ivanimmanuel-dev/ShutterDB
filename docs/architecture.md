# Architecture

The authoritative data is one append-only log. Public headers expose an owning `DB` with a private implementation; storage, format, checksums and index types remain in `src/`. There are no background workers.

```text
put/remove -> validate and reserve index node -> encode -> append -> optional OS sync -> index
get        -> ordered in-memory key index -> file offset -> decode and CRC -> owned value
open       -> stable sidecar lock -> recover replacement -> scan and CRC -> index
```

The index is a `std::map` from owned key bytes to offset, sequence and record/value sizes. Choosing a tree gives predictable lookup behavior without hash-collision sensitivity. Values are read on demand. Startup reads each file byte and applies O(log K) index operations per record: O(B + N log K) time for B bytes, N records and K live keys, ignoring key-comparison length. Memory is O(K + total live key bytes + maximum record size). No persisted index or read cache exists in v0.1.

## Writing and failure

Validate arguments and reserve a new map node before appending. Encode a complete bounded record, write the header and payload through unbuffered OS calls, and sync if requested. Only then update the in-memory index. OS partial writes and EINTR are handled explicitly. An append, flush or compaction error poisons the handle when the outcome could be ambiguous; subsequent operations require reopening. There is no attempt to roll back a possibly durable write by truncating it after a failed sync.

DELETE appends a tombstone and removes the index entry. Deleting a missing key returns false and creates no record. Statistics count all historical PUT/DELETE records and the byte size of current live records, including their headers. Reclaimable bytes exclude the 32-byte file header.

## Stable locking

The canonical database path determines a permanent `.lock` sidecar. Linux uses a nonblocking `flock`; Windows uses `LockFileEx`. The lock persists across data-file replacement. OS ownership disappears when a process dies, while the sidecar pathname remains. Hardlinked database and lock files are rejected. Symbolic database aliases are canonicalized; path parents and sidecars must be in a trusted local directory.

An open `DB` has an exclusive process lock. `inspect` takes a shared lock and opens database bytes read-only. It can create the lock sidecar, so its directory must permit this when no sidecar exists. It rejects an active writer instead of producing a moving-target report. All operations on one handle use one mutex; this is serialized thread safety, not parallel reads. Do not use a handle inherited across `fork`. Do not replace files or change aliases while a database is open. Network filesystems and mixed Windows/WSL access to the same open database are unsupported.

## Compaction protocol

1. Verify the complete original while holding the stable lock.
2. Exclusively create `.compact`, write a new file header and live records in sequence order.
3. Scan the complete temporary file; sync it.
4. Sync the original; copy it into `.backup.tmp` and sync that copy.
5. Rename the copy to `.backup`; sync the parent directory on POSIX.
6. Close data handles, keeping the lock open. Atomically replace the primary with `.compact` on POSIX; use `MoveFileExW` with replacement/write-through on Windows.
7. Sync the parent directory, reopen the primary, install its verified index.
8. Remove the backup and sync the parent directory again.

Failures leave a poisoned handle and recovery evidence. On the next open, a valid primary wins and the backup is removed. If the primary is absent or corrupt and the backup validates, the backup is restored. If neither validates, opening fails. Unsupported-format, resource-limit or I/O errors while scanning the primary do not trigger fallback: they cannot establish that rollback is safe. Abandoned `.compact` and `.backup.tmp` files are removed while locked. These suffixes and `.init` are reserved; do not use them for unrelated files.

This conservative algorithm needs temporary disk space approximately equal to the old log plus the compacted log. A successful compact can temporarily require total space approaching three times the original. No disk-space preflight can guarantee later writes succeed; errors preserve the original or backup. New files use private POSIX permissions (0600); compaction does not preserve arbitrary custom file metadata.

## New database publication

A new header is written and synced in `.init`, then renamed into place and the directory synced. An interrupted initialization can be retried under the same lock. Existing zero-length or truncated files are never silently reinitialized.
