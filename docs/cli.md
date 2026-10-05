# Command-line reference

`shutter` reads and writes ShutterDB files. Use `--db FILE` to select a database;
the default is `app.shdb`. Only `init` creates a missing database.

| Command | Usage |
|---|---|
| Create | `shutter init FILE` |
| Write text | `shutter set KEY VALUE --db FILE` |
| Write binary | `shutter set KEY --value-file INPUT --db FILE` |
| Read | `shutter get KEY --db FILE` |
| Delete | `shutter delete KEY --db FILE` |
| Statistics | `shutter stats --db FILE` |
| Verify | `shutter verify --db FILE` |
| Compact | `shutter compact --db FILE` |
| Recover incomplete tail | `shutter recover --db FILE` |
| Version | `shutter version` |

## Binary values

```sh
shutter set thumbnail --value-file thumbnail.bin --db app.shdb
shutter get thumbnail --raw --db app.shdb > restored.bin
```

`get --raw` writes the value bytes without a trailing newline. Text output adds a newline.
Use `--` before keys or values beginning with `--`:

```sh
shutter set --db app.shdb -- --key --value
```

## JSON output

Add `--json` for machine-readable output. `get` encodes values as hexadecimal:

```json
{"found":true,"encoding":"hex","value":"776f726c64"}
```

`stats --json` and `verify --json` include file size, record counts, live keys and the
valid-prefix length. Errors include a category, message and byte offset. CRC errors also
include expected and actual checksums. Format verification reports are written to stdout;
command errors are written to stderr.

## Writes and recovery

Writes synchronize by default. `set --buffered` and `delete --buffered` skip the per-write
sync; OS or power failure may lose recent buffered writes.

`verify` scans the file without changing database bytes. Normal commands reject an
incomplete tail. `recover` explicitly truncates that tail and synchronizes the result;
complete records with bad checksums remain errors. Make a copy of a closed database
before recovery. See [durability](durability.md).

## Exit codes

| Code | Meaning |
|---:|---|
| 0 | Success |
| 1 | Key not found |
| 2 | Invalid arguments |
| 3 | I/O, resource or other runtime error |
| 4 | Corrupt or unsupported database format |
| 5 | Database locked |

Run a complete example on temporary files:

```sh
python3 tools/demo.py build/shutter
```

With Visual Studio, use `build/Release/shutter.exe`.
