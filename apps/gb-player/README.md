# gb-player

`gb-player` is the native Qt 6 Quick desktop player (display name configured by the
`GB_PLAYER_DISPLAY_NAME` CMake cache variable, provisionally "GoBro"). It opens original GoPro
recordings read-only and plays them without conversion:

- **MAX dual-stream `.360`**: both HEVC lens tracks are decoded in lockstep and reconstructed on
  the GPU with the face-specific EAC mapping verified in M3. The view can be rectilinear,
  fisheye, stereographic, Little Planet, the full equirectangular panorama, or either raw track.
- **Single-stream video** (for example a MAX "superwide" MP4): shown flat with zoom and pan. No
  projection is inferred from the capture mode.

## Use

```sh
gb-player recording.360
gb-player --time 120 --view panorama recording.360
gb-player --play --decoder-threads 4 recording.360
gb-player --benchmark 60 --seeks 20 --time 60 recording.360 > benchmark.json
gb-player --screenshot view.png --time 5 recording.360
```

| Input | Action |
|---|---|
| Space, double-click | play / pause |
| Left / Right | seek −5 s / +5 s |
| `,` / `.` or `Ctrl+←` / `Ctrl+→` | step one frame back / forward (pauses) |
| Drag, wheel | look around and change FOV (spherical modes); pan and zoom (2D modes) |
| 1–8 | rectilinear, fisheye, stereographic, Little Planet, panorama, raw 1, raw 2, original |
| R / F / Ctrl+O | reset view / full screen / open file |

Seeking is accurate. The displayed frame is the latest frame whose presentation time is not after
the requested time. Frame stepping and the timeline both use the first stream's integer ticks.

## Panels

- **Telemetry**: synchronized to the displayed frame. It shows the nearest recorded `GPS5`
  record: GPS UTC (marked recorded GPSU or derived), validity, fix, DOP, latitude, longitude,
  WGS 84 altitude, 2D/3D speed, and the azimuth of the 2D speed vector (`0` north, `90` east,
  `180` south, `270` west) with its 16-point compass direction, e.g. `67.5°  ENE`. That azimuth is the direction of travel derived from neighbouring
  distinct positions, not the camera's pointing direction; it is unavailable when stationary
  (2D speed ≤ 0.01 m/s) or when no distinct neighbouring fix exists. It also shows the record's tick offset from the frame and its
  timing uncertainty. Missing data is shown as unavailable, invalid fixes are highlighted, and a
  reported gap is named. Positions are never interpolated.
- **Metadata**: file and stream properties, the ISO BMFF track table, and the GPMF tree of the
  `gpmd` sample covering the playback time. Unknown keys are flagged.
- **Performance**: displayed, presented and decoded rates, dropped frames (late or not drawn),
  decoder underruns, queue depth, unpaired frames, seek latency, texture upload time, GPU time
  (OpenGL timer queries), process CPU, RSS, and the OpenGL renderer.

The timeline marks `gpmd` sample positions, GPS coverage, and reported telemetry gaps.

## Benchmark report

`--benchmark SECONDS` waits for the first drawn frame and loaded metadata. It then plays from
`--time` for the requested wall-clock duration, pauses, and performs `--seeks` random accurate
seeks (deterministic with `--seed`). Finally it prints a `gb.player.benchmark/v1` JSON report and
exits non-zero if any seek shows a frame that does not cover its target. Seek latency runs from
the request to the first drawn frame. CPU is process CPU time over wall time. GPU time measures
texture upload plus drawing on the GPU. The report contains no telemetry values. Results describe
the machine they were measured on; see `research/findings/` for recorded runs.

## Requirements and limitations

- Requires an OpenGL 3.3 (or OpenGL ES 3.0) Qt Quick scene graph for the picture. With the
  software backend, decoding, presentation and telemetry still work, but no image is shown.
- No audio yet: playback runs on a wall clock. Hardware decoding (VA-API), edit lists,
  multi-chapter grouping, photos, maps, and export are not implemented.
- MAX reconstruction requires the verified two-stream geometry. The near-black `BACK` face of the
  fingerprinted sample is shown as recorded.
- Fisheye-family views keep the shared reference convention of normalizing each axis separately,
  so non-square views are elliptical.
