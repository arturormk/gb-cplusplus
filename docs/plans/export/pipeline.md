# Export: pipeline

## Overview

One export job produces each output frame like this:

```text
SequenceReader ──FrameSet──▶ FrameRenderer (offscreen GL, player shader) ──▶ picture area
      │                                                                          │
      ├──AudioBlocks──────────────────────────────────────────┐                  ▼
      │                                                       │      Compositor (QImage, QPainter)
      └──recording time──▶ telemetry lookup ──▶ overlay painters ──▶   picture + panel + overlays
                                                              │                  │
                                                              ▼                  ▼
                                                     AAC encoder        RGB→YUV, H.264/HEVC
                                                              └──▶ MP4/MKV muxer ◀┘
```

A still is the same path for one frame, with a still writer at the end instead of the encoder.

The job runs in `gb-render` ([cli.md](cli.md)). It takes the combined, validated settings
([settings.md](settings.md)) and a recording path, and has no other inputs, which is what makes an
export repeatable, as the M6 gate requires. It runs headless: the tool starts Qt with the
`offscreen` platform, which is enough for `QPainter`, fonts and an offscreen OpenGL context, if
spike 1 confirms it ([phases](phases.md#phase-0-spikes-measure-first)).

## Where the code goes

| Component | Location | Depends on |
|---|---|---|
| `RecordingTimeline`, `SequenceReader`, chapter positions | `lib/gb-media` | FFmpeg (existing) |
| `VideoWriter` (encode and mux) | `lib/gb-media` | FFmpeg (existing; adds libavcodec encoders, no new packages) |
| EXIF writer | `lib/gb-image` | nothing (stays dependency-free) |
| Ground distance ↔ zoom | `lib/gb-geo` | nothing (stays dependency-free) |
| Settings parsing and validation, framing geometry, overlay layout, map scale curve | new `lib/gb-export`, no Qt | `gb_media`, `gb_telemetry`, `gb_geo` |
| `FrameRenderer` (shader, textures, draw), overlay painters, compositor, export job | new `lib/gb-render` | Qt GUI (`QOpenGLExtraFunctions`, `QPainter`), `gb_tiles` |
| `gb-render` | `tools/gb-render` | `gb_export`, `gb_render` (no Qt Quick) |
| `gb-player` | `apps/gb-player`: uses `FrameRenderer` and the painters; adds Capture mode | `gb_render`, `gb_export` (settings text), `gb_image` (EXIF) |

Keeping settings, geometry, layout and the scale curve in a Qt-free library lets them be
unit-tested like the format libraries, and keeps the rule that libraries hold confirmed behaviour
while tools present it. The tool and its library share a name, as `tools/gb-metadata` and
`lib/gb-metadata` do.

### What `gb-player` shares, and what changes in it

The player and the exporter must draw the same picture and the same overlays for the same
moment, so the drawing code is shared rather than duplicated:

- The shader, texture upload and draw code move from `video_view.cpp` into `FrameRenderer`.
  `VideoView` keeps its render node and calls it.
- The drawing in `MapView` and `ProfileView` moves into painters (below). The items keep their
  interaction, follow animation and caching, and call the painters.

Both are refactors with no visible change, checked with the player's smoke, audio-gating and
GUI-stall tests, its benchmark, and image comparison before and after. The visible changes to the
player are [Capture mode](capture.md) and the chapter prefix on its clock. A capture uses the same
`FrameRenderer`, still writer and settings code as `gb-render --still`.

## Video renderer

### Choice

The player already renders every view in one fragment shader that ports the CPU reference
(ADR-0007). Export uses **that shader, offscreen**:

- The upload and draw code in `apps/gb-player/video_view.cpp` (texture setup, `uploadFrame`,
  uniforms, `draw`) moves into a `FrameRenderer` class in `lib/gb-render`. It takes a current GL
  context, a `FrameSet` and a set of view parameters, and draws into the bound framebuffer at a
  given pixel size. `VideoView`'s render node becomes a thin wrapper around it. This is a
  refactor with no change in behaviour, checked by image comparison before and after.
- Export creates its own `QOpenGLContext` on a `QOffscreenSurface`, renders into a framebuffer
  object at the output size, and reads the result back.
- `uSize` is currently in logical pixels. For export it is the framebuffer size in pixels. The
  shader's maths depends only on the aspect ratio, so the picture is the same.

The CPU reference (`project_equirectangular` over a CPU reconstruction) is too slow for video:
ADR-0007 records that it cannot keep up with playback at MAX resolution. It stays the
correctness reference.
Offscreen rendering makes an **automated GPU-versus-CPU comparison** possible for the first
time, which closes the architecture risk "agreement is checked by recorded image comparison, not
by an automated test" (see [phases](phases.md#testing)).

The CPU path is also the fallback for a headless machine with no usable OpenGL. It is correct
but slow, and `gb-render` says so when it is used.

### Framing parameters

The view parameters are the player's: mode, yaw, pitch, roll and FOV for spherical views, zoom and
pan for flat ones, and the canvas aspect ratio, which stands for the player's `uSize`. The crop
needs one small shader change: a `uViewCrop` rectangle (x, y, w, h in canvas units) maps the
output's texture coordinate onto the canvas, `vUv' = uViewCrop.xy + vUv · uViewCrop.zw`, before
the existing projection maths runs with the canvas size. The player draws with the whole canvas
(0, 0, 1, 1), so Play mode is unchanged.

Because the crop is applied before projection, the export is computed from the source at the
output resolution, never scaled up from the screen. The CPU reference gets the same crop
parameter, so the GPU-versus-CPU test covers it, and the two implementations stay in step as
ADR-0007 requires.

### Readback and colour

The first version reads RGBA with `glReadPixels` into a `QImage` (`Format_RGBA8888`). The
compositor draws the overlays on it, and swscale converts it to limited-range BT.709 YUV 4:2:0
for the encoder. Output is tagged BT.709 in the stream and the container. Later optimisations,
if measurements need them, are double-buffered pixel-buffer readback, and RGB→YUV in a second
shader pass after overlays are uploaded as a texture.

## Overlay painters

`MapView` and `ProfileView` are `QQuickPaintedItem`s that already draw with `QPainter`. Their
drawing moves into plain painter classes that take a `QPainter`, a target rectangle, a scale and
a scene description:

- `MapPainter`: tiles, route runs (travelled and remaining), current marker and course arrow,
  scale bar, attribution. Its scene is center, zoom, route, current record and the tile source. This is
  BLUEPRINT §9's reusable map scene: "UI and offscreen output should interpret this same scene".
- `ProfilePainter`: height or speed against time, with a cursor and the current-record dot. For
  export, its time window is the export range.
- `TelemetryTextPainter` (new): the chosen fields, units and time zone, in a readable style with
  a translucent backing when drawn over the picture.
- `AttributionPainter`: always drawn when the map is on, in the map's corner or at the bottom of
  the frame. It cannot be turned off.

`MapView` and `ProfileView` keep their interaction, follow animation and caching, and call the
painters for drawing. The export compositor calls the same painters. There is one drawing
implementation for each overlay.

Two behaviours of the live views need a decision for export, because export must be
deterministic:

- `MapView` low-pass filters the course arrow over wall-clock time. Export filters it over
  *media* time with the same constant, computed from the frame sequence, so a re-export gives
  the same pixels.
- `MapView` follow mode glides towards the marker over wall-clock time. The export map is always
  centred exactly on the marker, and its scale comes from the precomputed scale curve below.

### Telemetry rules carry over

Export draws telemetry with the player's rules (ADR-0004, ADR-0007, ADR-0009). Values come from
the recorded GPS record nearest to the frame's PTS (`gb::telemetry::nearest_point`), with no
interpolation. A record without a valid fix, or a time inside a reported gap, is drawn as
unavailable ("—"), never as zero. The map breaks the route at invalid stretches.

### Layout

`gb_export` computes the layout from the settings and the output size: the picture rectangle, the
panel rectangle for `beside`, and each overlay's rectangle, all in pixels. It rounds them to
whole pixels and keeps the picture area's size even. Layout is pure geometry and is unit-tested.
`--show-layout` draws these rectangles with their names.

## Map scale

The export map is centred on the current record, north up, at a scale that is fixed or automatic
([cli](cli.md#the-map-scale)). Scale is stored as the ground distance across the map's width, in
metres.

**From ground distance to tiles.** At zoom z and latitude φ, one Web Mercator tile pixel covers
40,075,016.7 m · cos φ / (256 · 2^z). A map *W* tile pixels wide showing *D* metres therefore needs
zoom z = log2(40,075,016.7 · cos φ · W / (256 · D)). *W* is the map's width in output
pixels divided by the tile scale (below). z is fractional: tiles are taken at the nearest whole
zoom and scaled, as `MapView` already does, and clamped to the provider's zoom range. The
conversion lives in `gb_geo` next to the existing Mercator code and is unit-tested.

**Tile scale.** To keep the map's look independent of resolution, tiles are drawn at
output height / 1080 output pixels per tile pixel. A 4K export shows the same streets and label
sizes, relative to the frame, as a 1080p one, with softer tiles, and a `--draft` export looks like
the final one. Sharper tiles at high resolution (one tile pixel per output pixel, more detail and
smaller labels) can be an option later.

**The automatic scale curve.** It is computed once, before any frame is rendered, from the GPS
records only:

1. For each output frame time *t*, take the records with a valid fix in [*t*, *t* + look-ahead]
   on the recording timeline, across chapter boundaries, and beyond the end of the range when the
   recording continues. The required half-width is the largest ground distance from the current
   record to any of them, along either axis of the map, plus the margin. With no valid record
   ahead, the requirement is the closest limit.
2. Clamp to the closest and widest limits.
3. Smooth in log scale, in media time, with two rules: the map is never narrower than required
   (the route ahead stays in view), and it zooms in no faster than the response setting allows.
   Because the whole curve is known, zooming out starts ahead of the moment it is needed instead
   of jumping at it. A small dead band stops the map from breathing with GPS noise.
4. Where the current record has no valid fix, hold the last value.

The curve is deterministic, so an export repeated from the same settings gives the same frames.
Stills, contact sheets, drafts and the full export use the same curve, so a
still at a position shows exactly the map the video will have there. For a ~20-minute recording at
18 GPS records per second it is a few tens of thousands of records, which takes milliseconds.

## Map tiles in export

Export fetches the tiles it needs and caches them, so a map overlay is complete without the user
having to view the route first. This has to stay within the OSM tile policy, which forbids bulk
downloading, and within ADR-0009, which says the player never prefetches. BLUEPRINT §9 also says
"never bulk-fetch official OSM tiles for rendering a video". The design below aims at the
narrowest fetch that still gives a complete map, and the ADR for it amends ADR-0009 and that line
of BLUEPRINT explicitly.

1. **Exactly the tiles drawn.** Before rendering, the job computes the viewport of every output
   frame from the position and the scale curve, and merges the tiles they draw. No margin around
   them, no zoom levels that are not drawn, no ancestors. An automatic scale draws several zoom
   levels along the way, so it needs more tiles than a fixed one. Because tiles are drawn at a
   scale tied to the frame height, the set is the same at every output resolution.
2. **Fetched in a separate stage.** The tile stage runs before rendering, never inside the frame
   loop, which keeps the render deterministic. It can run on its own with
   `--prepare-tiles DIR`, which fills a tile directory and stops
   ([cli](cli.md#map-tiles)). An export with `--map-tiles DIR` then runs with no network at all.
   Without a directory, the same stage runs at the start of the export, against the user's tile
   cache.
3. **Cache first.** Tiles already present are used. When fetching into the user's cache, stale
   tiles are revalidated with a conditional request, as in the player. A prepared directory is
   used as it is, stale or not, so it reproduces the same map every time.
4. **Through the same `TileFetcher`**, so the same rules apply: the project User-Agent, at most
   two requests at a time, HTTP caching rules, and the failure back-off. There is no separate,
   faster download path. A tile directory uses the `gb_geo` `TileCache` layout, so the fetcher
   writes into it unchanged, and `tiles.ini` records the provider and its attribution.
5. **A cap.** `--dry-run` reports how many tiles are present and how many would be fetched.
   Above `--max-tiles` (proposed default: 300), the tile stage stops before any request, with the
   count and the suggestion of a wider closest limit or a smaller map. A fixed 1 km-wide map
   along a 30 km route needs very roughly 200 tiles.
6. **Missing tiles.** With `--map-tiles`, a tile missing from the directory is an error before
   rendering starts, unless `--allow-missing-tiles` is given. With `--map-no-network`, or a
   failed fetch, a missing tile is drawn as a visible hatched placeholder, never left blank or
   filled from another zoom without saying so, and the count is reported. `--require-tiles`
   makes that an error instead. Fetching is on by default, as in `gb-player`. BLUEPRINT §12 asks
   analysis tools to avoid the network by default, and `gb-render` is not an analysis tool. The
   ADR records this.
7. **Attribution** is always drawn when the map is on, from the provider, or from `tiles.ini`
   for a tile directory.

Only tile addresses are sent, as when viewing the map, never the recording or its track. The
tile requests do reveal the route's area to the tile server, and `--dry-run` and the report say
so next to the fetch count.

## Timing

- **One output frame per source frame set.** The output frame rate is the source's exact
  rational rate. The output time base is the source time base, and each frame's PTS is its
  recording time minus the start of the range. No frames are duplicated, dropped or retimed.
- **Faster than real time** (`[output] speed` above 1): the output keeps the source frame rate
  and output frame *k* is the first source frame set at or after *k* × `speed` frame periods
  into the range, written at PTS *k* frame periods, so the others are dropped before they are
  rendered or encoded (they are still decoded). Telemetry is still looked up at each kept frame's
  source PTS. Up to 4× the audio is time-stretched at its pitch by FFmpeg's `atempo` filter;
  faster exports have no audio track, since a stretch that keeps a quarter of the sound or less
  is no longer listenable.
- The range is snapped to source frames: the in point to the first frame set whose PTS is not
  before it, the out point to the last whose PTS is before it. `--dry-run` shows the snapped
  range and frame count.
- The reader's accurate seek already returns the frame set at or before a target (ADR-0007). The
  job seeks to the in point and discards sets before it.
- **Audio** comes from `AudioReader` with sample-accurate seek. The job encodes exactly the
  samples covering `[first frame PTS, last frame PTS + frame duration)`, rescaled to the sample
  rate. It re-encodes to AAC rather than copying packets, so the cut is sample-exact. The audio
  track starts at PTS 0 with the video, and the encoder's priming delay is recorded in the
  container's edit list as FFmpeg's MP4 muxer does.
- Telemetry is looked up at each frame's *source* PTS, so an exported frame shows exactly what
  the player shows at the same moment. That is the M6 gate.

## Chapter sequences

`gb::media::chapter_sequence` already finds the chapters of a recording, and the track export
already walks them. Video export needs a timeline across them.

- `RecordingTimeline` (in `gb_media`): the ordered chapters, each with its duration and its
  offset on the recording timeline, in one common time base (the first chapter's video time
  base, when all agree; export refuses chapters whose geometry, layout or time base differ).
- `SequenceReader`: wraps one `SynchronizedReader` at a time. It opens the next chapter when the
  current one ends, and adds the chapter offset to each frame set's PTS. `SequenceAudioReader`
  does the same for audio, in samples.
- Telemetry is extracted per chapter, as now, and each point's media time is shifted by its
  chapter's offset.

**Measured on one recording.** The offset of chapter *n+1* is the sum of the video durations
of the chapters before it. This was confirmed on a three-chapter H.264 MP4 recording
([findings](../../../research/findings/2026-10-31-gopro-chapter-boundaries.md)): video, audio and
`gpmd` durations of each full chapter are exactly equal (707.2 s), the timecode of each chapter
continues the previous one to the frame, and GPS UTC puts each boundary at 707.200 s and
707.203 s, within its ±0.07 s jitter. `.360` and HEVC chapters are assumed to behave the same
until a multi-chapter `.360` recording confirms it. The timeline checks the rule on every
recording it joins (each chapter's audio and `gpmd` durations against its video duration, and
the timecode continuity) and warns, rather than guesses, when it does not hold.

Audio is joined by concatenation. Each chapter's audio fades in from near zero over its first
8–10 ms, so there is a short dip at every boundary. That is what the camera recorded, and export
keeps it rather than smoothing it over. The last chapter's audio ends one AAC frame (21.3 ms)
before its video. Export ends the audio track there and does not pad it.

Every chapter has a container location tag, which is another reason the writer copies no source
metadata (see below).

## Encoder

`gb::media::VideoWriter` wraps libavformat and libavcodec:

- Video: H.264 through `libx264` or HEVC through `libx265`, as chosen with `--codec`
  (`yuv420p`, CRF, preset, no tuning). HEVC in MP4 is tagged `hvc1` so that Apple players accept
  it. Encoders are looked up by name at run time. A missing encoder is reported, not substituted
  silently.
- Audio: FFmpeg's native `aac` encoder, stereo, at the source sample rate.
- Container: MP4 with `+faststart`, or MKV. The writer copies **no metadata** from the source.
  It writes the encoder tag and a creation time only. In particular there is no GPMF track, no
  `©xyz` location atom and no GPS of any kind in exported videos.
- Encoding runs on its own thread with a bounded queue between the compositor and the encoder,
  like the decode queue in the player.

`libx264` and `libx265` are GPL. The project is GPL-3.0-or-later and links Ubuntu's FFmpeg, which
is already built with them, so `docs/dependencies.md` needs a note, not a new package.

## Still writer

- **PNG** through the existing `write_png` path (from the composited `QImage`).
- **JPEG** encoded with FFmpeg's `mjpeg` encoder, or `QImageWriter`, with a chosen quality.
- **GPS EXIF**, only with `--gps-exif` (`gps_exif = yes`):
  - for a photo source, the photo's own GPS IFD position, altitude and time;
  - for a video frame, the nearest recorded GPS record, but only if it has a valid fix and is
    within one record interval of the frame. Its latitude, longitude, altitude, speed, and UTC
    date and time (from GPSU) go into the GPS IFD. Without a valid record the still is written
    without GPS, and a warning gives the reason.
  - `DateTimeOriginal` and `OffsetTimeOriginal` are written from the same UTC time when it is
    known.
  - `gb_image` gets a small, bounded **EXIF writer** that builds an APP1 segment (TIFF header,
    IFD0, EXIF IFD, GPS IFD) and splices it after SOI, replacing any JFIF APP0. It is tested by
    round-tripping through the existing `gb::image::parse_exif`.
- Without the option, an exported still carries no EXIF at all, and no camera serial numbers or
  other identifying metadata from the source. PNG gets no GPS in the first version (`eXIf` can
  come later).

## Performance expectations

Measured so far: decoding both MAX HEVC streams gives about 88 synchronised frame pairs per
second with all cores (M3), and playback at 25 fps uses about 2.3 cores (M4). A 1080p `libx264`
encode at `medium` competes for the same cores, so export of a MAX recording is expected to run
at roughly real time or slower. The first spike measures decode + render + readback + encode
throughput on the sample in `__stuff/`, and the result decides whether readback optimisation is
needed before the first release.
