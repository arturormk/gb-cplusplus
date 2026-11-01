# Export: phases, testing and open questions

Each phase ends with something usable, tests, and updated docs, following BLUEPRINT §14. The
phases are ordered so that the riskiest unknowns are measured before code depends on them, and
so that a still export works end to end before video does.

## Phase 0: spikes (measure first)

Each spike is recorded in `research/findings/` with commands and results.

1. **Headless OpenGL.** Can a Qt 6.2 `QOffscreenSurface` + `QOpenGLContext` render the player
   shader into a framebuffer object on Ubuntu 22.04 Mesa (a) under X11, (b) with
   `QT_QPA_PLATFORM=offscreen`, (c) with EGL surfaceless and no display at all, and (d) in CI
   (llvmpipe)? This decides whether `gb-render` and the GPU-vs-CPU test can run headless.
2. **Throughput.** Decode + offscreen render + readback + 1080p `libx264` and `libx265` encode of
   `__stuff/GS040047.360` and `__stuff/GH050083.MP4`, frames per second and cores used, at
   `veryfast` and `medium`.
3. **Chapter boundaries. Done for MP4** (2026-10-31): a three-chapter H.264 recording in
   `__stuff/export/` is exactly contiguous in video, audio, timecode and GPS UTC, with a short
   audio fade-in at each chapter start. See
   [`2026-10-31-gopro-chapter-boundaries.md`](../../../research/findings/2026-10-31-gopro-chapter-boundaries.md).
   Still to do: the same check on a multi-chapter `.360` recording.

## Phase 1: shared renderer

**Done (2026-10-31)**, with the parts of phase 2 that Capture mode needs: the chapter positions,
the EXIF writer, the INI syntax reader and the Capture settings writer in `lib/gb-export`, and
Capture mode itself (ADR-0012). Still to do in phase 2: settings validation and combining, the
`--print-settings` family, and `tools/gb-render --still`, with the test that compares it with a
capture. The GPU-versus-CPU comparison is not done yet: the offscreen test compares the GPU
renderer with itself (a crop against the whole canvas), not with `project_equirectangular`.

- Move shader, upload and draw from `video_view.cpp` into `lib/gb-render`'s `FrameRenderer`, with
  the `uViewCrop` rectangle. `VideoView` uses it with the whole canvas. No visible change.
- Add an offscreen render path and an automated test that renders synthetic frames in every
  mode, with and without a crop, and compares them against the CPU reference within a documented
  tolerance. This replaces the manual image comparison (architecture risk).
- Check the player before and after with its smoke, audio-gating and GUI-stall tests, the
  benchmark, and an image comparison.

## Phase 2: stills, in `gb-player` and `gb-render`

- `lib/gb-export`: the INI parser and writer, settings validation and combining (`--settings`,
  options, `--set`), `--print-settings`, `--save-settings`, `--example-settings`. Canvas and crop
  rules, aspect ratio and even-size rules.
- Chapter positions (`c2:00:05:31.400`): parse and format in `gb_media`, and the chapter prefix on
  `gb-player`'s clock.
- EXIF writer in `gb_image`, with round-trip tests through `parse_exif`.
- **Capture mode** in `gb-player`: Play/Capture selector, frozen canvas, aspect ratio and panel,
  capture rectangle with handles and keys, capture of the displayed frame (JPEG, PNG, GPS EXIF),
  *Set start* / *Set end*, the live settings text with Copy and Save as.
- `tools/gb-render` with `--still` for photos and video frames.
- Result: framed stills from the player, and the same stills from the command line.

## Phase 3: video, one chapter

**Done (2026-10-31)** (ADR-0013): `VideoWriter`, the export loop, `.partial` and rename,
`--overwrite`, Ctrl+C, `--start`/`--end`/`--duration`, `--no-audio`, `--draft`, `--dry-run`, and
settings validation, combining and `--print-settings`. No separate encoder thread yet.

- `VideoWriter` in `gb_media` (H.264 or HEVC, AAC, MP4 or MKV), the export job with its encoder
  thread, progress on stderr, Ctrl+C, `.partial` and rename, `--overwrite`.
- `--start`, `--end`, `--duration` within one chapter, `--no-audio`, `--draft`, `--dry-run`.
- Result: a framed clip with or without sound, no overlays.

## Phase 4: text and graph overlays

**Text done (2026-10-31)** (ADR-0013): fields with labels, anchors, sizes, two colours and an
outline, over the picture or on a left, right, top or bottom panel; units and time zone. Graphs
and `--show-layout` remain.

- Extract `ProfilePainter` from `ProfileView`, with no visible change. Add
  `TelemetryTextPainter`.
- Layout in `gb_export` (over and beside, anchors, `after`, panel order), the compositor in the
  export job, `--show-layout`.
- Units and time zone.

## Phase 5: the map overlay and its scale

**Fixed scale done (2026-10-31)** (ADR-0013): `[map] width`, `track`, `shape = square | circle`,
the tile stage with `--max-tiles`, `--map-no-network` and `--require-tiles`. **Automatic scale
done (2026-11-01)**: `[map] width = auto` with `ahead`, `closest` and `widest`
(`gb::geo::ScaleCurve`), its range in `--dry-run`. Activity presets, a response setting, the scale
bar, `--contact-sheet` and tile directories remain.

- Extract `MapPainter` from `MapView`, with no visible change. Add the scale bar and
  `AttributionPainter`. (Done early for Capture mode's side panel: `paint_map` and
  `paint_attribution` in `gb_render`, which the map pane now draws with; no scale bar yet.)
- Ground distance ↔ zoom conversion in `gb_geo`. The fixed scale, then the automatic scale curve
  in `gb_export`, with the activity presets, its summary in `--dry-run`, and `--contact-sheet`.
- The tile stage: compute the tiles drawn, fetch missing ones through `TileFetcher` before
  rendering, `--max-tiles`, `--map-no-network` with placeholders, `--require-tiles`. Tile
  directories: `--prepare-tiles DIR` (resumable, copies from the user's cache, writes
  `tiles.ini`) and `--map-tiles DIR` (offline, checked before rendering,
  `--allow-missing-tiles`). ADR amending
  ADR-0009 and BLUEPRINT §9.
- Tune the activity presets on real recordings: the three-chapter MP4 in `__stuff/export/`, and
  others as they are added. Record the values chosen and why in `research/findings/`.

## Phase 6: ranges across chapters

**Done for video, audio and telemetry (2026-10-31)** (ADR-0013): `RecordingTimeline` joins the
chapters one reader at a time; the consistency checks of chapter audio and `gpmd` durations are not
implemented yet.

- `RecordingTimeline`, `SequenceReader`, `SequenceAudioReader` and rebased telemetry, built on the
  rule measured in spike 3, with the consistency checks and warnings.
- Ranges, contact sheets and the scale curve across chapter boundaries.

## Phase 7: the M6 gate

- `--manifest` and `--json`. The `gb-render` README.
- The M6 gate run on the samples, recorded in `research/findings/`. BLUEPRINT M6 status,
  `docs/architecture.md`, `docs/dependencies.md` and the README updated.

## Testing

Following `docs/testing.md`: synthetic fixtures generated during CTest, no private media in the
tree.

| What | How |
|---|---|
| Settings | INI syntax (comments, lists, units, C-locale numbers, size limits), every validation error with file and line (unknown section or key, duplicate key, aspect/size mismatch, crop outside the canvas or of the wrong aspect ratio), combining files, options and `--set`, and `--save-settings` → `--settings` giving identical settings. |
| Framing and layout | Crop and canvas aspect rules, even-size rounding, over and beside rectangles for every aspect ratio and panel side. |
| GPU vs CPU | Offscreen render of synthetic six-face and equirectangular frames in every mode, against `project_equirectangular`, within a stated tolerance. Skipped, and reported as skipped, without a GL context. |
| Encoder | For H.264 and HEVC: encode an FFmpeg-generated fixture range, read it back with `video_streams` and `SynchronizedReader`: frame count, exact rebased PTS sequence, frame rate, size, BT.709 tags, no source metadata. |
| Audio sync | A generated fixture with a white flash and a click at the same instants. In the export, the click's sample position must match the flash frame's time within one audio frame, at the start, middle and end of the range, and across a chapter boundary. |
| Chapter positions | Parse and format round-trip; short forms; file-name form; a chapter that does not exist, or a time past the chapter's end, is an error naming the chapter's length; recording time ↔ chapter position across boundaries. |
| Range snapping | In and out points between frames and inside a GOP give the documented first and last frames. |
| Chapters | Synthetic chapter files named `GSnnmmmm.360`: continuous PTS across the boundary, telemetry rebased, a chapter with different geometry rejected. |
| Overlays | Painter output at two resolutions gives the same layout (rectangles scale exactly). Missing data draws "—", not zero. Missing tiles draw the placeholder and are counted. |
| Map scale | Ground distance ↔ zoom at several latitudes. On synthetic routes: a stop zooms in to the closest limit, a fast straight run zooms out to the widest, a U-turn inside the look-ahead keeps both legs in view at every frame, zoom-in never exceeds the response rate, invalid fixes ahead are ignored, and the curve is identical on recomputation. |
| Tile stage | The computed tile set for fixed and automatic scales on a synthetic route, with no margin or extra zoom. Against a local test server (as `gb_tiles_tests` does): only uncached tiles are requested, never more than two at a time, and a failure gives a placeholder, not a stalled export. `--prepare-tiles` resumes after an interruption, fetches only the difference after a settings change, and gives the same set at two output resolutions. `--map-tiles` makes no network request (the test server sees none), reports missing tiles before rendering, and refuses a directory from another provider. |
| EXIF | Written GPS IFD parses back to the same values. No EXIF when the option is off. Disabled with no valid fix. |
| Gate | `gb-render --manifest` on a fixture with synthetic GPS: each frame's record index equals `nearest_point` at its source PTS. On the private samples, the same check plus a visual comparison with the player at chosen timestamps. |
| CLI | `gb-render` on the fixture: a still, a contact sheet, a short draft clip, a dry run, and the error paths (bad position, existing output without `--overwrite`, `--max-tiles` exceeded, Ctrl+C leaving no partial file). |
| Capture | Offscreen `gb-player` test: enter Capture mode on the fixture, set an aspect ratio and a rectangle, capture, and compare the image with `gb-render --still` run on the copied settings. The rectangle keeps its place on the picture when the window is resized. |

## ADRs to write

- **Export as a command-line tool**: `gb-render` with INI settings, and what `gb-player` shares
  with it.
- **Capture mode**: the canvas and crop model, stills from the player, and the settings sidebar.
- **Export rendering**: offscreen GPU rendering with the shared player shader, CPU reference as
  oracle and fallback, QPainter overlays shared with the live views.
- **Export map scale**: always centred, scale as ground distance, fixed or automatic from a
  look-ahead curve computed in advance, with limits.
- **Map tiles in export**: fetch exactly the tiles drawn, before rendering, through the
  rate-limited fetcher, up to a limit, optionally as a separate step into a tile directory that
  the export then uses offline; cache-only on request; placeholders; mandatory
  attribution. Amends ADR-0009 and the BLUEPRINT §9 rule against fetching tiles for video
  rendering.
- **Recording timeline**: how chapters are joined, from the spike 3 measurements.
- **Location privacy in exports**: no location in videos, opt-in GPS EXIF in stills from
  recorded fixes only.

## Decided (2026-10-31)

- **Command line for video, Capture mode for framing.** Video export is `gb-render`. `gb-player`
  gets a Capture mode for framing and stills, which shows the framing as settings. The separate
  `gb-export` application and a full Export mode are dropped.
- **Settings in INI**, not JSON or YAML: easiest to write by hand and no new dependency.
- **Resolution** is an option and a setting, always at the configured aspect ratio.
- **Codec**: H.264 or HEVC, chosen with `--codec`.
- **Map tiles** are fetched and cached when not cached already, within the limits in
  [pipeline](pipeline.md#map-tiles-in-export). For long exports they can be fetched first into a
  directory (`--prepare-tiles`), which the export then uses offline (`--map-tiles`).
- **Map scale** is fixed or automatic (look-ahead with closest and widest limits), and the map
  always tracks the current position.
- **Chapter positions** are `c2:00:05:31.400`, readable from `gb-player`.
- **Chapter sample**: a three-chapter MP4 recording is in `__stuff/export/`, and spike 3 used it.

## Open questions

1. **Multi-chapter `.360`.** The chapter rule is confirmed for MP4 only. A recording of two or
   more `.360` chapters would confirm it for the MAX dual-stream layout.
2. **Tile limit.** 300 tiles by default is a first guess. It should be checked against a real
   automatic-scale export of the sample recording.
