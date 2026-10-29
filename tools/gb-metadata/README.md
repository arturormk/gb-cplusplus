# gb-metadata

`gb-metadata` is a read-only, dependency-free inventory and inspection tool for timed GoPro GPMF
metadata stored in an ISO BMFF `gpmd` track.

```sh
gb-metadata recording.360
gb-metadata --json recording.360
gb-metadata --json --sample 0 recording.360
gb-metadata --sample 0 --key GPS5 recording.360
gb-metadata --values --key GPS5 recording.360
gb-metadata --dump-raw payloads recording.360
```

The default view inventories every key, raw type and structure size, occurrence and record count,
sample range, units, and estimated record rate. `--sample` emits the complete nested KLV tree for
one payload, including source offset, raw bytes, decoded values, scaled values, units, and unknown
keys. Friendly sample output labels documented layouts such as the five `GPS5` components and
marks unknown keys without hiding their decoded storage values. `--key` filters text output;
`--values --key FOURCC` explicitly prints matching values across every timed payload. JSON uses
`gb.metadata/v1` for inventories and `gb.metadata.sample/v1` for a selected sample.

## Friendly value output

`--sample` is an explicit request to show values from one timed payload. It prints the nested
device/stream structure, storage type and repeat count, documented field labels, normalized units,
and the encoded values. This illustrative excerpt comes from the synthetic test fixture, not the
private sample corpus:

```text
GPS5 (int32, structure=20, repeat=1) - GPS position and speed
  value: latitude=40.1234567 deg, longitude=-3.1234567 deg,
         altitude=721.304 m, speed_2d=2.184 m/s, speed_3d=2.3 m/s
  encoding: sibling SCAL applied to int32 storage
  raw values: [401234567, -31234567, 721304, 2184, 230]
```

Known storage types are still useful when a key's meaning is not known. Such entries are not
silently omitted or given guessed labels:

```text
ZZZZ (uint8, structure=1, repeat=2) [UNKNOWN KEY: storage decoded, semantics unknown]
  raw values: [170, 187]
  raw bytes: aabb
```

`--sample INDEX --key FOURCC` narrows one payload. `--values --key FOURCC` walks the whole track
and can be very large; it reports the containing sample's integer decode time and duration before
each match. These are media-track ticks, not UTC or proven sensor capture times.

Raw export is opt-in and writes one `.gpmf` file per selected timed sample. The original recording
is never modified. Exit status is `0` for success, `2` for command-line misuse, `3` for file I/O,
`4` for corrupt container/GPMF input, and `5` for unsupported input or missing `gpmd` metadata.

## Current limitations

- ISO BMFF sample tables must use the implemented `stsc`, `stsz`, `stco`/`co64`, and `stts` forms.
- Complex (`?`) and compressed (`#`) values retain their complete raw bytes but are not decoded.
- Scaling uses the sibling `SCAL` tuple and reports values as decimal strings to avoid silently
  introducing a binary floating-point JSON contract.
- EXIF, XMP, MPF, JPEG APP6, static `moov/udta` metadata, and GPMF clock synchronization are not
  interpreted by this tool yet; their structural presence remains visible through `gb-inspect`.
- Record-rate estimates divide observed records by media-track duration. They are descriptive,
  not proof of perfectly periodic sampling or capture-clock alignment.
- Whole-track `--values` output can be very large and can reveal private coordinates or sensor
  data. It therefore requires an explicit `--key`; inventory remains the default.
- `--key` and `--values` are friendly-text filters and cannot be combined with `--json`; JSON
  consumers can filter the unchanged lossless schemas directly.
