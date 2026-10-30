# GoPro MAX (2025) sample findings

Date inspected: 2026-10-29

Evidence status: confirmed for these five files only

Repository base commit: `4acf11c6944adccbc45f7a2287b7685432d73972`

`gb-inspect`: 0.1.0 from the uncommitted working tree based on that commit

This report records the first inspection of original GoPro MAX (2025) media available to this
project. It is not a general claim that every firmware version or shooting mode has the same
layout. Inspection was read-only. The files remain under `__stuff/` and are not repository
fixtures.

Exact GPS coordinates, camera/lens serials, media IDs, and scene content are deliberately omitted.
The hashes below identify the evidence without publishing those private fields.

## Tools

- `gb-inspect 0.1.0`
- FFprobe 4.4.2 (`4.4.2-0ubuntu0.22.04.1+esm17`)
- ExifTool 12.40
- file 5.41
- ImageMagick `identify` from the local Ubuntu environment

FFprobe and ExifTool are independent reference tools here; they are not project dependencies.

## Evidence inventory

| File | Supplied mode | Exact bytes | SHA-256 |
|---|---|---:|---|
| `GS040047.360` | 360 video | 4,008,703,896 | `6c410ceaf750a299937a637489247ce4cbf56b1fde9616da2838ddd63ac7ca63` |
| `GH050083.MP4` | superwide video | 3,075,396,507 | `b39c7e9bdc3cbb4ebfba7cd6808ee02a7402c0f543cb95424a0a177d27a5e1b7` |
| `GP__0011.JPG` | normal photo | 2,679,418 | `538732f35988358c0f9b17258377a6b34ae0b2409f9add8a74c5e872765607ed` |
| `GP__0026.JPG` | superwide photo | 2,617,132 | `c9bc791bf13747dcf4f80c70176a5224adaeb62fccb9be1488665f008429821` |
| `GS__0033.JPG` | 360 photo | 5,082,107 | `8dfedf5367bfcbd256d7d320574b1762dfbc2034013c693eb545d8193756d81b` |

All five identify the camera as `GoPro Max`, firmware `H19.03.02.06.00`, and GoPro metadata
version `8.1.2`.

## Common video container layout

Both video files are ISO BMFF/MP4 with major and compatible brand `mp41`. Their complete
top-level order is:

```text
ftyp -> mdat -> moov
```

The `moov` box is at the end, so an inspector or player must seek near the end before it can read
the track tables. The media data is not fragmented. Both samples use 32-bit `stco` chunk offsets,
not `co64`; even the `.360` sample remains below the 4 GiB unsigned-offset boundary. This may help
explain camera chaptering near 4 GB, but chaptering behavior is not proven by one file.

Both `moov/udta` boxes contain these GoPro-specific children:

```text
FIRM LENS CAME SETT MUID HMMT BCID GUMI GPMF free
```

The MP4 additionally starts `udta` with a QuickTime `©xyz` location box. At the corresponding
position, the `.360` file has a `free` box. This does **not** mean the `.360` lacks GPS: its `gpmd`
track contains GPS samples.

`gb-inspect` measured under 0.01 seconds and about 3.6 MiB maximum RSS per video because it seeks
over `mdat` rather than reading the multi-gigabyte payload.

### `GS040047.360`: native 360 video

- Duration: 482.560 s (8:02.560)
- Container bitrate reported by FFprobe: 66,457,292 bit/s
- Top-level boxes:
  - `ftyp`: offset 0, size 20
  - `mdat`: offset 20, size 4,007,643,255
  - `moov`: offset 4,007,643,275, size 1,060,621
- Seven tracks are present.

| FFprobe index | Type/tag | Time base | Duration / samples | Important properties |
|---:|---|---|---|---|
| 0 | HEVC `hvc1` video | 1/90000 | 482.560 s; 12,064 frames | Main, 4096×1344, 25 fps, BT.709, about 30.002 Mbit/s |
| 1 | AAC-LC `mp4a` audio | 1/48000 | 482.560 s; 22,620 frames | 48 kHz stereo, about 189 kbit/s |
| 2 | `tmcd` data | 1/90000 | 482.560 s; 1 sample | GoPro timecode |
| 3 | `gpmd` binary data | 1/1000 | 482.560 s; 464 samples | GoPro timed metadata/GPMF, about 86.6 kbit/s |
| 4 | `fdsc` data | 1/90000 | 482.560 s; 69,834 samples | Handler `GoPro SOS`, about 18.6 kbit/s; semantics not yet established |
| 5 | HEVC `hvc1` video | 1/90000 | 482.560 s; 12,064 frames | Main, 4096×1344, 25 fps, BT.709, about 29.999 Mbit/s |
| 6 | PCM `in32` audio | 1/48000 | 482.560 s; 23,162,880 samples | 48 kHz, 4 channels, little-endian, 6.144 Mbit/s; handler `GoPro AMB` |

The two HEVC tracks have equal dimensions, frame count, frame rate, start time, duration, and
nearly equal bitrates. This strongly identifies them as the synchronized front/back lens streams,
consistent with the user-supplied 360 mode. The exact lens-to-track assignment remains unknown.

FFprobe identifies track 6 as signed 32-bit PCM, and its bitrate exactly equals
`48000 * 4 * 32`. ExifTool reports 16 bits per sample for the same track. The raw sample entry must
be examined before treating either textual report as authoritative.

FFprobe reports an all-zero display matrix on the second HEVC stream, with an invalid/sentinel
rotation value. Code must not interpret that matrix as a normal transform. The movie header's raw
`next_track_ID` is 6 although track IDs 6 and 7 exist; readers must enumerate actual `trak` boxes
instead of trusting that field as an upper bound.

### `GH050083.MP4`: superwide video

- Duration: 543.560 s (9:03.560)
- Container bitrate reported by FFprobe: 45,263,029 bit/s
- Top-level boxes:
  - `ftyp`: offset 0, size 20
  - `mdat`: offset 20, size 3,074,732,028
  - `moov`: offset 3,074,732,048, size 664,459
- Five tracks are present.

| FFprobe index | Type/tag | Time base | Duration / samples | Important properties |
|---:|---|---|---|---|
| 0 | H.264 `avc1` video | 1/90000 | 543.560 s; 13,589 frames | High profile, 1920×1080, 25 fps, BT.709, about 45.004 Mbit/s |
| 1 | AAC-LC `mp4a` audio | 1/48000 | 543.531 s; 25,478 frames | 48 kHz stereo, about 189 kbit/s |
| 2 | `tmcd` data | 1/90000 | 543.560 s; 1 sample | GoPro timecode |
| 3 | `gpmd` binary data | 1/1000 | 543.560 s; 523 samples | GoPro timed metadata/GPMF, about 50.7 kbit/s |
| 4 | `fdsc` data | 1/90000 | 543.560 s; 39,592 samples | Handler `GoPro SOS`, about 9.3 kbit/s; semantics not yet established |

No projection or field-of-view value understood by these tools proves “superwide”; that mode
label comes from the capture context. Unlike the native `.360`, this file is already a single
ordinary 16:9 video stream and has no `GoPro AMB` PCM track.

### Embedded telemetry

ExifTool's embedded scan (`-ee`) confirms both `gpmd` tracks carry:

- GPS latitude, longitude, altitude, 2D speed and 3D speed;
- GPS fix mode, horizontal positioning error, and GPS date/time;
- accelerometer and gyroscope arrays; and
- sample times and repeated timestamps.

| File | `gpmd` samples/documents | Extracted GPS point occurrences | Accelerometer groups | Gyroscope groups | Timestamp occurrences |
|---|---:|---:|---:|---:|---:|
| `GS040047.360` | 464 | 8,771 | 464 | 464 | 7,424 |
| `GH050083.MP4` | 523 | 9,841 | 523 | 523 | 9,414 |

An “accelerometer group” or “gyroscope group” may contain an array of readings, so these counts
are not sensor sample rates. M1 now inventories precise GPMF types, scales, units, nesting, and
integer media timing; gap policy and capture/UTC clock mapping remain M2 work. GPS coordinates
were inspected only to establish presence and were not copied into this report.

#### Verified `GPS5` storage layout

The native M1 parser establishes the same `GPS5` storage layout in both video samples. Each record
is a 20-byte structure containing five big-endian signed 32-bit integers. The enclosing stream
supplies these `SCAL` divisors and units, in order:

| Component | Meaning | Divisor | Unit |
|---:|---|---:|---|
| 0 | latitude | 10,000,000 | degrees |
| 1 | longitude | 10,000,000 | degrees |
| 2 | altitude | 1,000 | metres |
| 3 | 2D speed | 1,000 | metres/second |
| 4 | 3D speed | 100 | metres/second |

The component meanings follow the reviewed
[official GoPro GPMF reference](../dependencies.md#gpmf-reference-review); the raw type, structure
size, divisors, units, and repetition were independently observed in both fingerprinted samples.
For example, their first payloads contain 18 and 19 `GPS5` records respectively. The record count
varies between payloads, so it must not be assumed to be constant. `GPSF`, `GPSP`, and `GPSU` are
separate sibling records; `GPS5` alone does not establish fix validity, precision, UTC alignment,
or a gap policy. Those interpretations belong to the M2 telemetry/timing work.

## JPEG findings

All three are baseline 8-bit JPEGs using YCbCr 4:2:2. They contain big-endian EXIF, GoPro APP6
metadata, MPF metadata, an embedded EXIF thumbnail, and a second MPF image used as a dependent
preview. The second image is not evidence of a second raw lens capture. ExifTool emits a minor
`Unrecognized MakerNotes` warning for each file, so its decoded GoPro fields are not a complete
inventory of the opaque metadata.

| Property | Normal `GP__0011.JPG` | Superwide `GP__0026.JPG` | 360 `GS__0033.JPG` |
|---|---:|---:|---:|
| Main dimensions | 2704×2028 (4:3) | 4320×1440 (3:1) | 5760×2880 (2:1) |
| GoPro resolution tag | `5.5MP_W` | `6.2MP_P` | `16.6MP_S` |
| XMP GPano | absent | present | present |
| GPano projection | — | equirectangular | equirectangular |
| GPano full canvas | — | 5760×2880 | 5760×2880 |
| GPano crop `(left, top, width, height)` | — | `(720, 720, 4320, 1440)` | `(0, 0, 5760, 2880)` |
| GPS EXIF | not observed | not observed | present (values redacted) |
| MPF images | 2 | 2 | 2 |
| Dependent preview length | 420,970 bytes | 615,930 bytes | 395,658 bytes |

The superwide photo is therefore encoded as the centered crop of a full-size equirectangular
canvas, not merely as an untagged wide JPEG. Its 4320/5760 width ratio arithmetically corresponds
to 270 degrees on an ideal equirectangular mapping, but the effective optical field of view and
stitch/crop policy need independent validation before that is exposed as a semantic claim. The
360 photo occupies the complete 2:1 GPano canvas and declares two source photos.

JPEG marker inventory:

| File | APP1 | APP2 | APP6 |
|---|---|---|---|
| Normal | one segment, 62,304 bytes | 176 bytes (MPF) | 818 bytes (GoPro) |
| Superwide | two segments, 1,737 and 53,273 bytes | 176 bytes (MPF) | 1,162 bytes (GoPro) |
| 360 | two segments, 1,733 and 60,365 bytes | 176 bytes (MPF) | 1,162 bytes (GoPro) |

The extra APP1 segment in the panoramic photos carries XMP GPano data. ExifTool exposes GoPro
“Geometry Calibrations”, “Back Lens”, and “Front Lens” device containers in all three APP6
payloads, but the default report does not provide a complete numeric calibration model.

### Follow-up validation with `gb-inspect` 0.2.0

The dependency-free JPEG path added after the initial external-tool inspection identifies all
three files by their SOI signature rather than extension. It validates every marker boundary,
finds the two directly appended codestreams in each file, and independently reproduces the primary
and preview offsets, lengths, dimensions, precision, and component counts above. It also reports
the structural presence of EXIF, XMP, MPF, and APP6 without exposing their private values.

Measured warm-cache runs took approximately 0.02–0.05 seconds per photo with about 3.6–3.8 MiB
maximum RSS. This measurement describes these samples and development hardware only.

## Reproduction commands

Run from the repository root. Full ExifTool output contains private serial and location values;
do not publish it without redaction.

```sh
build/dev/tools/gb-inspect/gb-inspect __stuff/GS040047.360
build/dev/tools/gb-inspect/gb-inspect __stuff/GH050083.MP4

ffprobe -v error -show_format -show_streams -of json __stuff/GS040047.360
ffprobe -v error -show_format -show_streams -of json __stuff/GH050083.MP4

exiftool -G1 -a -s __stuff/GP__0011.JPG __stuff/GP__0026.JPG __stuff/GS__0033.JPG
exiftool -ee -api LargeFileSupport=1 -G3 -s __stuff/GS040047.360
exiftool -ee -api LargeFileSupport=1 -G3 -s __stuff/GH050083.MP4

sha256sum __stuff/GS040047.360 __stuff/GH050083.MP4 \
  __stuff/GP__0011.JPG __stuff/GP__0026.JPG __stuff/GS__0033.JPG
```

## Implementation consequences and next experiments

1. Extend `gb_container` to parse `mvhd`, `tkhd`, `mdhd`, `hdlr`, and `stsd`; this will let
   `gb-inspect` reproduce the track table above without FFprobe.
2. Preserve duplicate video/audio roles and unknown `fdsc` data rather than selecting only the
   first playable stream.
3. Treat integer ticks plus rational time bases as authoritative. Do not reduce timing to rounded
   seconds.
4. Implement GPMF location separately from GPMF interpretation, and test `.360` GPS discovery
   without relying on a `©xyz` user-data box.
5. Add loss-aware APP6, EXIF, XMP, and MPF field parsers using synthetic fixtures. Extract any
   real payload fixture only after a privacy review; do not commit these original samples.
6. Decode representative frames from both HEVC tracks to establish lens ordering, packing, and
   reconstruction requirements. This report did not inspect scene pixels.
7. Determine the `fdsc`/`GoPro SOS` payload semantics and resolve the 32-bit-versus-16-bit PCM
   reporting discrepancy from raw sample descriptions.
8. Acquire additional files for other resolutions, frame rates, GPS-disabled cases, chapter
   boundaries, low-light modes, and firmware versions before generalizing.

## M3 frame-decode follow-up (2026-10-30)

`gb-frames` 0.5.0 directly enumerates and software-decodes both HEVC streams through FFmpeg 4.4.2.
At 5 s and 120 s, both tracks select the exact requested integer PTS and report equal 3600-tick
frame durations on their 1/90000 time bases. Separate PNGs, raw RGB24, and schema-versioned color
and timing reports require no intermediate conversion.

The corrected CPU transform follows the face-specific 4096×1344 geometry: stream 0 supplies
`LEFT / FRONT / RIGHT`, stream 5 supplies `DOWN / BACK / TOP`, and the additional 64 pixels are
retained overlap distributed through wrapped side-face duplicates. It applies the required face
rotations and equal-angular cube mapping rather than treating the tracks as generic cube-atlas
rows. Six-color synthetic fixtures verify that all faces reach their expected panorama regions,
and visual checks at 5 s and 120 s show an upright, continuously wrapped result.

The center `BACK` face of decoded stream 5 is already near-black in this particular recording;
its outer faces contain image data. The source diagnostic and raw extraction establish that the
black region precedes projection. The implementation preserves and reports it instead of filling
it, so it is an evidence-file content limitation rather than an unimplemented face mapping.

Reproduction without publishing images:

```sh
build/dev/tools/gb-frames/gb-frames --list --json __stuff/GS040047.360
build/dev/tools/gb-frames/gb-frames --json --stream 0 --time 5 \
  --output /tmp/stream0.png __stuff/GS040047.360
build/dev/tools/gb-frames/gb-frames --json --stream 5 --time 5 \
  --output /tmp/stream5.png __stuff/GS040047.360
build/dev/tools/gb-frames/gb-frames --json --reconstruct --stream 0 --stream 5 \
  --time 120 --width 2048 --height 1024 --output /tmp/panorama.png \
  __stuff/GS040047.360
build/dev/tools/gb-project/gb-project --time 5 --width 2048 __stuff/GS040047.360
```

## M1 native-tool validation

The M1 implementation completed the first four implementation consequences above. On both videos,
`gb-inspect` now reproduces FFprobe's track count, track order, codec FourCC, time base, integer
duration, sample count, handler role/name, and video geometry. It preserves `fdsc` as an unknown
metadata track and reports the raw PCM sample-entry value (16 bits), leaving the previously noted
FFprobe/ExifTool interpretation disagreement unresolved rather than choosing one report.

`gb-metadata` maps `gpmd` through the ISO BMFF sample tables and parsed every payload in both files.
The privacy-safe count comparison was:

| File | GPMF payloads | `GPS5` records | ExifTool embedded GPS | ACCL/GYRO tuple occurrences |
|---|---:|---:|---:|---:|
| `GS040047.360` | 464 | 8,771 | 8,771 | 464 / 464 |
| `GH050083.MP4` | 523 | 9,841 | 9,841 | 523 / 523 |

ExifTool also reports one MP4-level location value for `GH050083.MP4`; it is not part of the GPMF
count. The native inventory additionally preserves unknown keys and complex/compressed raw values,
reports `SCAL`-normalized primitives with units, and exposes integer sample ticks. Approximate
warm-cache inventory measurements were 2.7 seconds / 4.7 MiB for the `.360` and 1.5 seconds /
4.5 MiB for the MP4 on the development machine. These measurements are not performance promises.

Reproduction:

```sh
build/dev/tools/gb-inspect/gb-inspect __stuff/GS040047.360
build/dev/tools/gb-metadata/gb-metadata __stuff/GS040047.360
build/dev/tools/gb-metadata/gb-metadata --json --sample 0 __stuff/GS040047.360
build/dev/tools/gb-metadata/gb-metadata --sample 0 --key GPS5 __stuff/GS040047.360
```

The last command prints private coordinates to the terminal. Do not capture or publish its output
without an explicit privacy review. Use the default inventory when values are not required.

## M2 telemetry/timing validation

The native telemetry layer reproduces the established 8,771 and 9,841 `GPS5` records from the two
video samples. Every payload supplies a parseable `GPSU` anchor; the observed `GPSF` values classify
all records in these two captures as valid. Selected beginning, middle, and ending media ticks were
cross-checked against `gb-metadata` normalized GPS5 records without recording coordinates in this
report. `gb-time --at` returns the nearest recorded position and its integer-tick uncertainty; it
does not interpolate a route.

All mapped sample tables in both files have continuous decode timelines. The maximum absolute
difference between adjacent GPSU-anchor deltas and the corresponding media-time deltas was 61 ms
in each sample. This is a diagnostic observation, not a clock-accuracy guarantee. GPS5 records are
uniformly assigned within each GPMF payload, with one derived record interval reported as timing
uncertainty. No sensor capture-time or TimeWarp mapping is claimed.

Privacy-safe reproduction:

```sh
build/dev/tools/gb-telemetry/gb-telemetry --format json __stuff/GS040047.360 \
  | jq '{points: (.points | length), valid: ([.points[] | select(.validity == "valid")] | length)}'
build/dev/tools/gb-time/gb-time --json --at 5000 __stuff/GS040047.360 \
  | jq '{tracks: (.tracks | length), anchors: (.gps_clock.anchors | length), query_validity: .query.nearest.validity}'
```
