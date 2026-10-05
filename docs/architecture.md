# Architecture

ShutterDB stores data in an append-only log. Public headers expose `DB` and the bounded
`Cache`, both with private implementations. Storage, format, checksums and index types
live in `src/`.

```text
put/remove -> validate and reserve index node -> encode -> append -> optional OS sync -> index
get        -> ordered in-memory key index -> file offset -> decode and CRC -> owned value
open       -> stable sidecar lock -> recover replacement -> scan and CRC -> index
```

The index is a `std::map` from owned key bytes to offset, sequence and record/value
sizes. Values are read on demand. Startup reads each file byte and applies O(log K)
index operations per record: O(B + N log K) time for B bytes, N records and K live
keys, excluding key-comparison cost. Memory is O(K + total live key bytes + maximum
record size).

Reads use the indexed record size to fetch a complete record in one operation, then
validate its header, payload checksum and key before returning the value. CRC32C uses
x86-64 SSE4.2 instructions when detected at runtime, with a portable slicing-by-eight
implementation for other CPUs. Both produce the same format-v1 checksum.

Opening and verification read the log through a reusable 1 MiB window, growing it
for an individual record when necessary. Records in the window are checksummed
without allocating a separate payload buffer. Replay updates existing index entries
in place when a key is overwritten. Every header and payload is validated.

`Cache` wraps the database with an access-order list and a key-to-list lookup. It
evicts entries under capacity pressure and invokes ordinary verified compaction.
Read recency is kept in memory; restart order comes from record sequence numbers.

## Writing and failure

The write path validates arguments and reserves a new map node before appending. It encodes a bounded record, writes the header and payload through OS calls, synchronizes if requested, then updates the index. Partial writes and EINTR are retried. An uncertain append, flush or compaction failure invalidates the handle with `NEEDS_REOPEN`; a failed sync keeps bytes already written for recovery.

DELETE appends a tombstone and removes the index entry. Deleting a missing key returns false and creates no record. Statistics count all historical PUT/DELETE records and the byte size of current live records, including their headers. Reclaimable bytes exclude the 32-byte file header.

## Stable locking

The canonical database path determines a permanent `.lock` sidecar. Linux uses a nonblocking `flock`; Windows uses `LockFileEx`. The lock persists across data-file replacement. OS ownership disappears when a process dies, while the sidecar pathname remains. Hardlinked database and lock files are rejected. Symbolic database aliases are canonicalized; path parents and sidecars must be in a trusted local directory.

An open `DB` has an exclusive process lock. `inspect` takes a shared lock, rejects an active writer and opens database bytes read-only. It can create the lock sidecar, which requires directory write access. Operations on one handle use one mutex. Using a handle inherited across `fork` is unsupported. Filesystem requirements are in [durability](durability.md#filesystem-requirements).

## Compaction protocol

1. Verify the complete original while holding the stable lock.
2. Exclusively create `.compact`, write a new file header and live records in sequence order.
3. Scan the complete temporary file; sync it.
4. Sync the original; copy it into `.backup.tmp` and sync that copy.
5. Rename the copy to `.backup`; sync the parent directory on POSIX.
6. Close data handles, keeping the lock open. Atomically replace the primary with `.compact` on POSIX; use `MoveFileExW` with replacement/write-through on Windows.
7. Sync the parent directory, reopen the primary, install its verified index.
8. Remove the backup and sync the parent directory again.

On failure, the handle requires reopening and sidecars remain for recovery. A valid primary takes precedence over the backup. If the primary is absent or corrupt, a valid backup is restored; otherwise opening fails. Unsupported-format, resource-limit and I/O errors prevent fallback. Abandoned `.compact` and `.backup.tmp` files are removed while locked. These suffixes and `.init` are reserved.

Compaction needs additional disk space for a backup of the old log and the compacted output: total use can approach three times the original file size. New files use POSIX permissions 0600; custom file metadata is not preserved.

## New database publication

A new header is written and synced in `.init`, then renamed into place and the directory synced. An interrupted initialization can be retried under the same lock. Existing empty or truncated files are rejected.
