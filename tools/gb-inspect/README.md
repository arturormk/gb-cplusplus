# gb-inspect

`gb-inspect` identifies a file from its content and reports either its validated ISO BMFF box
hierarchy or JPEG marker/codestream structure. The concise text view is intended for a terminal;
`--json` emits a stable, machine-readable format-specific schema.

```sh
gb-inspect recording.360
gb-inspect --json recording.360
gb-inspect --json photo.jpg
```

Exit status is `0` for success, `2` for command-line misuse, `3` for a missing or unreadable
input, `4` for structurally corrupt/truncated input, and `5` for an unsupported input. Diagnostics
are written to stderr.

ISO BMFF output uses `gb.inspect/v1`. It walks common structural container boxes and reports track
IDs, handlers, codecs, integer duration/time bases, sample counts, video geometry, and basic audio
sample-entry fields. It maps bounded sample tables for `gpmd` consumers without reading media
payloads. Unknown boxes and tracks remain visible and are not assigned guessed semantics.

JPEG output uses `gb.inspect.jpeg/v1`. It validates marker lengths and entropy termination,
reports dimensions/precision/components, recognizes JFIF, EXIF, XMP, MPF and APP6 presence, and
enumerates directly appended JPEG codestreams such as GoPro's dependent preview. It deliberately
does not interpret EXIF/XMP/MPF/APP6 payload fields yet. Content detection does not rely on the
filename extension.
