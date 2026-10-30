# 2026-10-30 gb-player M5 map alignment and tile behaviour

Status: confirmed for the fingerprinted `GS040047.360` and `GH050083.MP4` samples on the M4
development machine; not a general performance claim.

Base commit: `f86c334` plus the uncommitted M5 working tree (`gb-player` 0.8.0, Debug build).
The machine and display are the same as in
[`2026-10-30-gb-player-m4-baseline.md`](2026-10-30-gb-player-m4-baseline.md). Window size
1600×960 with a map pane of about 440×380. Tiles came from `tile.openstreetmap.org` over the real
network. No coordinates, routes, tile addresses or screenshots are recorded here. Screenshots of
the map reveal the recorded location and must not be published.

## Map alignment gate

Each run played 30 s from 60 s, then ran the audio gating sequence and 20 random paused seeks
(seed 1). The map figures come from the benchmark `map` section. A sample counts as lagging when
the painted marker is more than two video frames from the record nearest to the displayed PTS.

| Input, tile cache | Media / wall s | Displayed fps | Dropped | Playback samples (lagging) | Pause / seek checks (mismatches) | Exact seeks | Tile requests |
|---|---:|---:|---:|---:|---:|---:|---|
| `.360`, warm | 29.96 / 30.00 | 24.97 | 0 | 312 (0) | 1 / 20 (0) | 20/20 | 0 (14 from disk) |
| MP4, warm | 29.56 / 29.58 | 24.95 | 0 | 307 (0) | 1 / 20 (0) | 20/20 | 0 (10 from disk) |
| MP4, warm | 29.76 / 29.80 | 24.97 | 0 | 310 (0) | 1 / 20 (0) | 20/20 | 0 (10 from disk) |
| MP4, empty | 29.72 / 29.76 | 24.97 | 0 | 309 (0) | 1 / 20 (0) | 20/20 | 10 downloaded |
| MP4, empty | 29.60 / 29.65 | 24.96 | 0 | 308 (0) | 1 / 20 (0) | 20/20 | 10 downloaded |

In every run the painted marker was the expected record in every sample: the maximum playback
lag was 0 ms. `.360` playback cost (242% of one core, 0.80 GiB peak RSS, 319 ms median seek) is
within the M4 baseline range.

A deliberately broken map, which did not repaint on position changes, failed the gate: 103/103
lagging samples, 4/10 seek mismatches, 1/1 pause mismatch, exit status 1. The fix was then
restored.

## Unexplained run

One earlier MP4 run with partly cached tiles (6 downloaded) failed. Media advanced only 23.1 s in
31.1 s of wall time, it showed 17.98 fps with 18 late drops, and 94,208 audio frames were heard
during the seek burst. The map was still aligned (237 samples, 0 lagging, 0 mismatches). Process
CPU was 72% instead of about 115%, which suggests fewer frames were requested. That matches the
compositor throttling of a background window noted in the M4 finding. This is a **hypothesis**,
because window visibility was not recorded. Four later MP4 runs were clean, including two with an
empty cache that downloaded tiles during playback.

## Tile policy behaviour

- First fetch: `Cache-Control: max-age` of about 12 hours and a quoted ETag were recorded in the
  `.meta` sidecar. The expiry follows the header, not the 7-day fallback.
- Every sidecar was then forced to expire. A 3 s `.360` run showed 13 stale tiles at once and sent
  13 conditional requests: 12 returned `304 Not Modified` and one tile was downloaded again.
- Requests carry `User-Agent: gb-player/0.8.0 (gb-cplusplus)` and no `Cache-Control` or `Pragma`.
  The unit test caught `CacheLoadControl=AlwaysNetwork` adding `Cache-Control: no-cache`, and it
  was removed before these measurements.
- Click-to-seek was driven through X11 test events on the real window. A click on the route end
  at 2:00 sought to 7:57.960. The video, telemetry (record 11 of `gpmd` sample 459, +2 ms) and
  marker agreed, and follow mode recentred the map.

## Reproduction

```sh
build/dev/apps/gb-player/gb-player --benchmark 30 --seeks 20 --time 60 \
  --map-cache /path/to/scratch/tiles __stuff/GS040047.360
build/dev/apps/gb-player/gb-player --benchmark 30 --seeks 20 --time 60 \
  --map-cache /path/to/scratch/tiles __stuff/GH050083.MP4
```

Use an empty `--map-cache` directory for cold runs. Keep the window visible and unobscured.

## Not established

The behaviour of recordings with invalid fixes or reported gaps was not checked on real media; both
samples are valid throughout their GPS coverage. These cases are covered only by synthetic unit
tests and code review. Repeated-coordinate passes were not exercised on real media. Other
providers, HiDPI rendering, Wayland, long sessions and cache growth were not measured.
