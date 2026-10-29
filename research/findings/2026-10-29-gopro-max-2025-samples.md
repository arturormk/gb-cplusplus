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
