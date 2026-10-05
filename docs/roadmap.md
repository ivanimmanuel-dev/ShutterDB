# Release scope and future work

## v0.1.0 — experimental

Persistent PUT/GET/DELETE, restart scanning, checksummed format v1, bounded parsing,
manual compaction with a durable backup, synchronization modes, typed errors, locking,
CLI and CMake packaging are the release scope. No new storage architecture was added
during release hardening. See the [gate ledger](release-blockers.md) and
[validation evidence](validation.md).

Format v1 is documented byte-for-byte. This release reads and writes v1 only; an unknown
version fails closed. No migration tool or indefinite on-disk compatibility promise is
made for future experimental versions. Keep backups before upgrades.

## Further validation

Physical power cuts, reordered block I/O, more filesystems/devices, independent durability
review, longer fuzzing and external integration experience would strengthen confidence.
These limitations remain visible in the [durability contract](durability.md).

## Ideas, not commitments

Read concurrency, startup improvements, segmented logs, snapshots and atomic batches
require measured need and a separate design review. Package-manager publication also
needs validation in each actual packaging ecosystem. None is part of v0.1 or started
as part of this release. The project has no SQL, networking, replication, LSM/SSTable
pipeline or background compaction.
