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
