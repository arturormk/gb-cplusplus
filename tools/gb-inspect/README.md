# gb-inspect

`gb-inspect` reads a file and reports its validated ISO BMFF identity, byte size, and recognized
box hierarchy. The concise text view is intended for a terminal; `--json` emits the stable,
machine-readable `gb.inspect/v1` schema.

```sh
gb-inspect recording.360
gb-inspect --json recording.360
```

Exit status is `0` for success, `2` for command-line misuse, `3` for a missing or unreadable
input, `4` for structurally corrupt/truncated input, and `5` for an unsupported input. Diagnostics
are written to stderr.

Current limitations: this thin slice recognizes an ISO BMFF file through `ftyp` and safely walks
common structural container boxes. It does not yet decode track/sample tables, codecs, metadata,
or vendor payloads, and has not been validated against original GoPro MAX (2025) media. Unknown
boxes remain visible as raw type/offset/size records but are not recursively guessed.
