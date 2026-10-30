# 2026-10-30 gb-player M4 playback baseline

Status: confirmed for the fingerprinted `GS040047.360` and `GH050083.MP4` samples on one
development machine; not a general performance claim.

Base commit: `bd9744f` plus the uncommitted M4 working tree (`gb-player` 0.7.0, Debug build of
project code, system FFmpeg 4.4.2). Sample hashes are listed in
[`../../docs/formats/gopro-max-2025-sample-findings.md`](../../docs/formats/gopro-max-2025-sample-findings.md).
No coordinates, routes, or scene images are recorded here.

## Environment

- Intel Core with 12 hardware threads, Comet Lake UHD Graphics (CML GT2), Mesa 23.2.1, OpenGL 4.6
  compatibility context; Ubuntu 22.04; Qt 6.2.4; X11 window 1600×960, device pixel ratio 1.
- Default view (rectilinear, 90° FOV); video area about 1140×845 logical pixels.

## Gate measurements

60 s of continuous playback starting at 60 s, then 20 random paused accurate seeks (seed 1):

| Input / decoder threads per stream | Displayed fps | Dropped | Underruns | Unpaired | Process CPU | GPU ms mean / max | Upload ms mean / max | Peak RSS | Seek median / p95 / max | Exact seeks |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `.360` dual HEVC 4096×1344, automatic | 24.97 | 0 | 0 | 0 | 232% of a core (19% of 12) | 2.42 / 3.91 | 2.90 / 10.3 | 790 MiB | 354 / 504 / 520 ms | 20/20 |
| `.360` dual HEVC 4096×1344, 4 | 24.97 | 0 | 0 | 0 | 232% (19%) | 1.04 / 3.67 | 2.96 / 6.96 | 428 MiB | 325 / 481 / 610 ms | 20/20 |
| MP4 H.264 1920×1080, automatic | 24.98 | 0 | 0 | 0 | 106% (9%) | 1.00 / 1.26 | 0.93 / 3.75 | 302 MiB | 129 / 199 / 204 ms | 20/20 |

Both recordings are 25 fps (3600 ticks at 1/90000). "Exact" means the displayed frame's
`[PTS, PTS + duration)` contains the target. Seek latency is dominated by decoding from the
preceding keyframe. The `.360` GOP is 93,600 ticks (1.04 s). Standalone reader measurements gave
88 synchronized `.360` frame pairs per second with automatic threading, so real-time playback
uses roughly 30% of the machine's decode capacity.

An earlier iteration presented frames from a free-running 4 ms timer. It showed 2–6 frames per
30 s on the MP4 that were presented but never drawn. Presenting once per Qt Quick frame removed
them (0 in three repeated 30 s runs and in the 60 s runs above).

## GPU reconstruction cross-check

At 120 s, the panorama view (1140×570 region of a window screenshot) was compared with
`gb-frames --reconstruct --stream 0 --stream 5 --width 1140 --height 570`. PSNR was 23.7 dB
unfiltered. After the same Gaussian low-pass on both images it was 31.4 dB (σ = 1) and 37.1 dB
(σ = 2). The difference image is concentrated in high-frequency texture, while smooth regions
are nearly identical. This is consistent with different bilinear sampling and aliasing when
reducing 4096-pixel strips to 1140 pixels. It is not consistent with a mapping or color error.
Black regions of the source (including the known near-black `BACK` face) appear identically in
both.

## Telemetry synchronization

At 120 s and 300 s (video PTS 10,800,000 and 27,000,000 at 1/90000), the telemetry panel showed
the GPS records at `gpmd` ticks 119,983 and 300,012 (1/1000; −17 ms and +12 ms from the frame),
each ±55 ticks, valid. `gb-time --json --at 120000` and `--at 300000` select the same records.
Coordinates were compared on screen only and are not recorded.

## Reproduction

```sh
build/dev/apps/gb-player/gb-player --benchmark 60 --seeks 20 --time 60 __stuff/GS040047.360
build/dev/apps/gb-player/gb-player --benchmark 60 --seeks 20 --time 60 --decoder-threads 4 \
  __stuff/GS040047.360
build/dev/apps/gb-player/gb-player --benchmark 60 --seeks 20 --time 60 __stuff/GH050083.MP4
build/dev/tools/gb-time/gb-time --json --at 120000 __stuff/GS040047.360
build/dev/apps/gb-player/gb-player --time 120 --view panorama --screenshot /tmp/gpu.png \
  __stuff/GS040047.360
build/dev/tools/gb-frames/gb-frames --reconstruct --stream 0 --stream 5 --time 120 \
  --width 1140 --height 570 --output /tmp/cpu.png __stuff/GS040047.360
ffmpeg -i /tmp/gpu.png -i /tmp/cpu.png -filter_complex \
  "[0:v]crop=1140:570:7:180,format=rgb24,gblur=sigma=2[a];[1:v]format=rgb24,gblur=sigma=2[b];[a][b]psnr" \
  -f null -
```

Screenshots include the telemetry panel and therefore private coordinates; do not publish them.

## Not established

Audio synchronization, hardware decoding, longer soak tests, other GPUs and drivers, Wayland, high
DPI displays, and recordings with other resolutions, frame rates, or chapter boundaries.
