# 2026-10-31 GoPro chapter boundaries

Status: confirmed for one three-chapter H.264 MP4 recording (GoPro MAX 2025, firmware
`H19.03.02.06.00`, 1920×1080 at 25 fps, AAC stereo 48 kHz). Not yet checked for `.360` chapters.
This is spike 3 of the [export plan](../../docs/plans/export/phases.md).

Base commit: `9361907` (`gb-time` 0.1.39, Debug build), FFprobe/FFmpeg 4.4.2. The files are
private and were read in place through the symbolic links in `__stuff/export/`. No coordinates,
places or wall-clock times are recorded here.

| File | First 16 MiB SHA-256 (prefix) | Size, bytes |
|---|---|---:|
| `GH010073.MP4` | `7c9dd4a1635a608f` | 4,000,966,727 |
| `GH020073.MP4` | `5a3e0d36fc5c1e2d` | 4,000,763,422 |
| `GH030073.MP4` | `99e4a48371abd48f` | 2,333,774,280 |

## Observations

**Stream durations.** From `ffprobe -show_entries stream=…`:

| Chapter | Video ticks (1/90000) | Video frames | Audio samples (1/48000) | `gpmd` ms | `gpmd` samples |
|---|---:|---:|---:|---:|---:|
| 1 | 63,648,000 (707.2 s) | 17,680 | 33,945,600 (707.2 s) | 707,200 | 680 |
| 2 | 63,648,000 (707.2 s) | 17,680 | 33,945,600 (707.2 s) | 707,200 | 680 |
| 3 | 37,123,200 (412.48 s) | 10,312 | 19,798,016 (412.459 s) | 412,480 | 397 |

Every stream starts at 0. In chapters 1 and 2, video, audio and `gpmd` durations are exactly
equal. 17,680 frames at 25 fps is exactly 707.2 s, and 33,945,600 samples is exactly 707.2 s
(33,150 AAC frames of 1,024). Only the last chapter's audio ends early, by exactly 1,024 samples (one
AAC frame, 21.3 ms), at the end of the recording.

**Timecode.** Each chapter's `tmcd` start timecode is the previous one's plus its duration, to
the frame: 707.2 s is 11 min 47 s and 5 frames, and both boundaries advance by exactly that.
Every chapter carries the same container `creation_time`, the recording start.

**GPS UTC.** For every GPSU anchor reported by `gb-time --json`, UTC minus media time was
computed. Its spread inside a chapter is ±0.07 s after the first anchor (the first anchor of
chapter 1 is 1.6 s off, as the receiver settles). The median over the last 30 anchors of one
chapter and the first 30 of the next gives the start offset of the next chapter on the UTC
clock: 707.200 s from chapter 1 to 2, and 707.203 s from chapter 2 to 3. Expected from
durations: 707.2 s. This is an independent clock, and it agrees within its jitter.

**Audio at the boundary.** The last 100 ms of one chapter and the first 100 ms of the next were
decoded with FFmpeg to float. Each chapter's audio starts near zero and rises to the level of the
signal over the first 8–10 ms (peak per millisecond: 0.002, 0.004, … reaching about 0.02 at
8 ms, against 0.01–0.03 in the previous chapter's last milliseconds). There is no silence gap and
no repeated material, but there is a short fade-in at every chapter start. That is consistent
with each chapter's AAC stream being encoded from a fresh encoder state. It was not yet checked
by listening.

**Location tag.** The container also has a top-level location tag (as the 2026-10-29 findings
noted for the other MP4). An export that copied container metadata would leak it.

## Conclusions

- Confirmed for this recording: chapters are contiguous. Chapter *n+1* starts on the recording
  timeline at the sum of the previous chapters' video durations, with no gap or overlap in
  video, audio or GPS, to frame precision (timecode) and within ±0.07 s by GPS UTC.
- Hypothesis: the same holds for `.360` chapters and for HEVC (`GX`) chapters. It needs a
  multi-chapter `.360` recording to confirm.
- Joining audio by concatenation leaves a dip of about 10 ms at each boundary. It is what the
  camera recorded, and the export plan does not try to hide it.
- Exported files must not copy source container metadata.

## Reproduction

```sh
for f in __stuff/export/GH0{1,2,3}0073.MP4; do
  ffprobe -v error -show_entries \
    stream=index,codec_type,time_base,start_time,duration_ts,nb_frames:stream_tags=timecode \
    -of compact "$f"
  build/dev/tools/gb-time/gb-time --json "$f" > "$(basename "$f").json"
done
# UTC minus media time per anchor: gps_clock.anchors[].utc - media_time_ticks / 1000
ffmpeg -v error -ss 707.1 -i __stuff/export/GH010073.MP4 -map 0:a:0 -f f32le tail.raw
ffmpeg -v error -i __stuff/export/GH020073.MP4 -map 0:a:0 -t 0.1 -f f32le head.raw
```
