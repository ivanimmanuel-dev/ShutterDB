# ShutterDB format v1

All integers are unsigned, fixed width and **little endian**. No struct layouts are serialized. There is no padding between records. The file starts with exactly one 32-byte header, followed by zero or more records. Format version and project version are independent.

## File header (32 bytes)

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 8 | ASCII `SHUTDB` followed by CR LF (`53 48 55 54 44 42 0d 0a`) |
| 8 | 2 | Format version, 1 |
| 10 | 2 | Header length, 32 |
| 12 | 4 | Flags, zero |
| 16 | 8 | Sequence watermark, initially zero |
| 24 | 4 | Reserved, zero |
| 28 | 4 | CRC32C of bytes [0,28) |

Compaction sets the watermark to the last allocated sequence. It preserves live record sequences in increasing order, even when they are below the watermark. The next write uses `max(watermark, largest record sequence) + 1`. This prevents sequence reuse when compaction produces an empty log. Exhausting uint64 sequence space is an error.

## Record header (40 bytes)

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `SHDR` |
| 4 | 2 | Record format version, 1 |
| 6 | 1 | Type: 1 PUT, 2 DELETE |
| 7 | 1 | Flags, zero |
| 8 | 8 | Nonzero sequence number |
| 16 | 4 | Key byte length |
| 20 | 4 | Value byte length |
| 24 | 4 | CRC32C of concatenated key and value |
| 28 | 4 | Total size = 40 + key length + value length |
| 32 | 4 | Reserved, zero |
| 36 | 4 | CRC32C of header bytes [0,36) |
| 40 | key length | Key bytes |
| 40 + key length | value length | Value bytes |

Keys must have 1–65,536 bytes. Values may have 0–16,777,216 bytes. A record is at most **16,842,792 bytes**. DELETE must have a zero-length value. Sequences must strictly increase between consecutive records; gaps are permitted. Unknown flags, reserved fields, types and unsupported versions are errors.

## Integrity and bounds

CRC32C uses the Castagnoli polynomial, reflected form `0x82f63b78`, initial value `0xffffffff`, final XOR `0xffffffff`. The ASCII check vector `123456789` produces `0xe3069283`. Header and payload CRCs cover every record byte.

The scanner validates the header CRC before using record lengths. It widens arithmetic
to uint64, checks limits and checks available file bytes by subtraction. Payloads are
validated in a reusable 1 MiB read window, which grows to fit a bounded record when
needed. Read and write offsets are restricted to signed 64-bit file positions.

## Recovery rules

- A complete, validated record is applied using latest-write-wins semantics.
- A partial record header at EOF is recoverable only if its available fixed fields, complete size fields and complete sequence are valid.
- A complete valid record header whose bounded payload extends beyond EOF is a truncated tail.
- A complete header CRC or payload CRC mismatch is corruption, including the last record.
- A truncated file header, unknown format or unexpected trailing bytes is an error.
- Scanning stops at the first error. Counts on an error report describe the scanned prefix.

An incomplete header cannot be fully checksummed, so damage matching a valid partial prefix can be classified as a tail. Suffix removal can resemble an interrupted append; loss at a record boundary is undetectable without external history. Set `recover_truncated_tail=false` to inspect incomplete files before repair.
