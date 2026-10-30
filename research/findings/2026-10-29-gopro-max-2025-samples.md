# 2026-10-29 GoPro MAX (2025) sample inspection

Status: confirmed for five fingerprinted private samples; not generalized to all modes or
firmware.

The reproducible finding, evidence hashes, commands, observations, privacy policy, hypotheses, and
next experiments are maintained in
[`../../docs/formats/gopro-max-2025-sample-findings.md`](../../docs/formats/gopro-max-2025-sample-findings.md).

Headline result: the native `.360` sample is an `mp41` ISO BMFF file with two synchronized
4096×1344 HEVC lens tracks, AAC stereo, timecode, `gpmd`, unknown `fdsc`, and four-channel PCM. The
ordinary MP4 has one H.264 track plus AAC/timecode/`gpmd`/`fdsc`. Both video files contain timed
GPS, accelerometer, and gyroscope metadata. The 360 JPEG is a full 5760×2880 equirectangular GPano
image, while the supplied superwide JPEG is a 4320×1440 crop within a declared 5760×2880 GPano
canvas.

Follow-up: `gb-inspect` 0.2.0 now validates the JPEG marker/codestream structure directly and
reproduces the primary and dependent-preview geometry without ExifTool or ImageMagick.

M1 follow-up: the in-tree container parser now independently reproduces all seven `.360` tracks
and all five MP4 tracks, including codec FourCC, exact integer time base/duration, sample count,
video geometry, audio fields, handler name, `gpmd`, and unknown `fdsc`. `gb-metadata` maps all 464
and 523 `gpmd` samples respectively and inventories GPMF without FFmpeg or ExifTool. Its `GPS5`
record counts are 8,771 and 9,841, exactly matching ExifTool's embedded GPS occurrences (the MP4
also has one separate top-level location tag). GPMF accelerometer and gyroscope tuple occurrences
match ExifTool's 464/464 and 523/523 groups. No coordinate or sensor value was copied into this
finding.

Both videos encode `GPS5` as repeated big-endian `int32[5]` records with observed divisors
`[10000000, 10000000, 1000, 1000, 100]` and units `[deg, deg, m, m/s, m/s]`. The documented
components are latitude, longitude, altitude, 2D speed, and 3D speed. Fix, precision, and UTC are
carried separately; this storage finding does not itself establish validity or clock alignment.

M2 follow-up: `gb-telemetry` reproduces both GPS5 counts and classifies the observed points using
the sibling GPSF fix value. `gb-time` maps GPSU anchors to integer media presentation ticks and
reports one-record timing uncertainty for the uniform within-payload assignment. All mapped track
sample tables are decode-continuous; adjacent GPSU versus media deltas differ by at most 61 ms in
both samples. Selected beginning, middle, and ending ticks were checked without copying private
positions or UTC values into this finding.

M3 `gb-frames` follow-up (2026-10-30): FFmpeg 4.4.2 software-decodes both 4096×1344 HEVC streams
directly from the original `.360`. At media times 5 s and 120 s, both selected frames have the
exact requested PTS (`450000` and `10800000` respectively), duration 3600 ticks, and time base
1/90000. PNG extraction preserves the reported full-range BT.709 source description.

The corrected CPU reference was visually checked at both times. It follows the published
face-specific layout: stream 0 contains `LEFT / FRONT / RIGHT`, stream 5 contains
`DOWN / BACK / TOP`, and the retained width is blended across wrapped side-face duplicates. This
corrects the earlier generic two-row cube-atlas assumption and produces an upright, navigable
equirectangular panorama with continuous wrapping.

A diagnostic source scan at 5 s found stream 5's center `BACK` face essentially black while its
outer faces contain image data. That condition exists before projection and is not caused by PNG
encoding or the face mapping. `gb-frames` and `gb-project` deliberately preserve and report it
instead of synthesizing pixels. Six-color synthetic fixtures independently exercise every mapped
face, so this evidence-file limitation does not leave the M3 reconstruction model unresolved.
