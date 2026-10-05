# Security and integrity reports

ShutterDB 0.1.0 is an experimental local storage library. There is no established supported release line or security response SLA yet.

Report suspected memory-safety, bounded-parser, locking or silent-data-loss defects privately through the repository host's private security-reporting feature **when the maintainer has enabled it**. If it is unavailable, ask the maintainer for a private contact without posting exploit details or private database contents. Use the contact options listed on the repository; no public disclosure is required to obtain a private channel.

Include the revision, compiler/OS/filesystem, operation sequence, sanitizer output, diagnostic offset and a minimal synthetic reproducer. Preserve your original file; use a redacted copy for investigation. Never upload a database containing secrets to a public issue.

The file format is designed to reject malformed lengths before allocation and detect accidental corruption. CRC32C is not cryptographic authentication. ShutterDB has no access-control layer or encryption, and assumes a trusted local parent directory. Applications remain responsible for OS permissions, backups and protecting secrets.

Dependencies must be pinned and licensed. CI must not execute untrusted pull-request code with publishing credentials. Any future format-recovery tool should fail closed by default and keep a copy before modifying data.
