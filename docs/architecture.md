# Architecture

ShutterDB stores data in an append-only log with an in-memory key index. `DB` owns the
storage; `Cache` adds access tracking and eviction. Both hide their implementations
behind the public headers. Storage, format and index types live in `src/`.

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

The write path validates arguments and reserves a map node before appending. It writes
the encoded header and payload, synchronizes if requested, then updates the index.
Partial writes and EINTR are retried. An append, sync or compaction failure with an
uncertain outcome marks the handle `NEEDS_REOPEN`; reopening scans the bytes left on disk.

DELETE appends a tombstone and removes the index entry. A missing key creates no record.
Statistics count historical PUT/DELETE records and the byte size of live records,
including their headers. Reclaimable bytes exclude the 32-byte file header.

## Stable locking

The canonical database path determines a permanent `.lock` sidecar. Linux uses
nonblocking `flock`; Windows uses `LockFileEx`. The lock stays held across data-file
replacement. Process exit releases ownership; the sidecar pathname remains.
Symbolic database aliases are canonicalized. Hardlinked database and lock files are
rejected. Parent directories and sidecars must be trusted.

`DB` takes an exclusive lock; `inspect` takes a shared lock and reads database bytes
without modification. Inspection can create the sidecar and requires directory write
access. One mutex serializes operations on a handle. Do not use inherited handles after `fork`.
See [filesystem requirements](durability.md#filesystem-requirements).

## Compaction protocol

1. Verify the complete original while holding the stable lock.
2. Exclusively create `.compact`, write a new file header and live records in sequence order.
3. Scan the complete temporary file; sync it.
4. Sync the original; copy it into `.backup.tmp` and sync that copy.
5. Rename the copy to `.backup`; sync the parent directory on POSIX.
6. Close data handles, keeping the lock open. Atomically replace the primary with `.compact` on POSIX; use `MoveFileExW` with replacement/write-through on Windows.
7. Sync the parent directory, reopen the primary, install its verified index.
8. Remove the backup and sync the parent directory again.

Failed compaction leaves sidecars for recovery and requires reopening. A valid primary
wins over the backup. If the primary is absent or corrupt, a valid backup is restored.
Unsupported-format, resource-limit and I/O errors prevent fallback. Abandoned `.compact`
and `.backup.tmp` files are removed while locked. These suffixes and `.init` are reserved.

Compaction stores the old log, a backup and the replacement: total disk use can approach
three times the original file size. New POSIX files use permissions 0600.
Replacement files do not preserve custom metadata.

## Creating a database

A new header is written and synced in `.init`, then renamed into place and the directory
synced. Interrupted initialization can be retried under the same lock. Existing empty
or truncated files are rejected.
