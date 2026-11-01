# ADR-0012: Capture mode, the canvas and crop model, and the shared renderer

Status: Accepted (2026-10-31)

## Context

The export plan ([`docs/plans/export/`](../plans/export/README.md)) makes video export a
command-line tool, `gb-render`, and gives `gb-player` a **Capture mode** for framing and stills
([`capture.md`](../plans/export/capture.md)). The framing chosen in the player must mean exactly
the same thing to `gb-render`, which has no window, and a still must be computed from the source
at its own resolution, not scaled up from the screen.

Until now the player's shader, texture upload and draw code lived inside `VideoView`'s
scene-graph render node, and could only draw into the window.

## Decision

- **Canvas and crop.** A still selects a rectangle of the **canvas**: the view as `gb-player`
  draws it, with its projection, angles, field of view, zoom and pan. The canvas is described by
  its aspect ratio only. The rectangle (`crop`) is in canvas units, 0..1 on each axis, and its
  aspect ratio in canvas pixels (`w · canvas / h`) is the picture's. The same model works for
  every view: a rectangle of a flat picture, a shifted-centre rectilinear picture, or part of a
  Little Planet.
- **The canvas is frozen** when Capture mode is first entered in a session, at the aspect ratio of
  the area the view has. In Capture mode the view is letterboxed to it, so resizing the window
  scales the canvas but never changes what is in it, and the rectangle keeps its place on the
  picture. With Square on, the canvas is square. `canvas` is written in the settings, so
  `gb-render` can reproduce it without a window.
- **Crop before projection.** The shader gets a `uViewCrop` rectangle. The output's texture
  coordinate is mapped onto the canvas, `canvasUv = uViewCrop.xy + vUv · uViewCrop.zw`, before
  the existing projection maths, which still uses the canvas aspect ratio. The player draws the
  whole canvas (0, 0, 1, 1), so Play mode is unchanged. Every output pixel is computed from the
  source.
- **A shared renderer.** The shader, texture upload and draw move from `apps/gb-player` into a new
  `lib/gb-render` (`gb::render::FrameRenderer`, Qt GUI only). The shader sources are compiled into
  the library, so it needs no Qt resources. `VideoView`'s render node keeps the scene-graph state
  handling and GPU timing and calls it. `OffscreenRenderer` owns its own OpenGL context on a
  `QOffscreenSurface`, renders into a framebuffer object at the output size and reads it back. It
  works on the GUI thread of the player, whose scene graph renders on another thread, and headless
  with `QT_QPA_PLATFORM=offscreen` (measured on Mesa with an Intel GPU and with llvmpipe).
- **Settings and geometry without Qt.** A new `lib/gb-export` holds the framing geometry (aspect
  ratios, even output sizes, side panels, crop fitting, moving and scaling, the resolution
  estimate), the `gb.export/1` INI syntax reader, and the writer for the parts Capture mode knows:
  `[range]`, `[view]`, `[frame]` and `[layout]`. The crop is written with as many decimals as it
  needs to keep the picture's aspect ratio within 0.1 % when read back.
- **Stills** are written by `gb::render::write_still`: JPEG with a chosen quality, or PNG, through
  a temporary file renamed into place. A suggested file name is never an existing file, and the
  name dialog (Shift+click) asks before replacing one. Without the option a still has no EXIF at
  all. With *Include GPS position (EXIF)* a JPEG gets a GPS IFD built by
  `gb::image::build_gps_exif`: for a video frame, from the nearest recorded GPS record, only when
  it has a valid fix and lies within one record interval of the frame; for a photo, from its own
  EXIF position. No camera make, model, serial or software is written. PNG gets no GPS.
- **The side panel** can show the route map and the telemetry text, drawn with QPainter by
  `gb::render::paint_panel` onto the composed frame. The map painter is extracted from the map
  pane (`paint_map`, `paint_attribution`), which now draws with it, so the pane and stills draw
  the same map; target pixels per map pixel are frame height / 1080. The map keeps the pane's zoom
  and is centred on the current position, north up, as the export plan specifies for `gb-render`.
  The telemetry comes from the same fix as the GPS EXIF, with the date and time in the computer's
  time zone. The panel geometry (a square map and the text area, margins of 24/1080 of the frame
  height) is in `gb_export`, so `gb-render` can reuse it. Capture fetches the map's missing tiles
  through the player's `TileFetcher`, with its rate limits, waits at most five seconds, and draws
  and reports what did not arrive as placeholders. The settings record the panel's contents as
  `[units]`, `[map] scale` and `[telemetry] fields`.
- **The displayed frame is captured**: the engine's current frame set, after pausing playback,
  not a frame decoded again.
- **Chapter positions** (`c2:00:05:31.400`) are parsed and formatted by `gb_media`, and the
  player's clock shows the chapter in front for a chaptered recording.

## Consequences

- The player and future exports draw with one implementation. An automated test renders
  synthetic frames offscreen and checks that a crop equals the same part of the whole canvas
  drawn at a size where the pixels line up, in the flat and MAX layouts and in four views. Play
  mode was compared before and after the move on the MAX sample in three views; the video area
  differs by at most 1/255.
- Offscreen rendering needs OpenGL 3.3 or OpenGL ES 3.0. Without it, capture reports the reason,
  and the tests that need it are reported as skipped.
- `gb-render --still` is not planned (amendment, 2026-11-01), so the test comparing a capture
  with it is dropped. Stills are written only by Capture mode; `gb-render` previews with a short
  `--draft` segment.
- The resolution warning is an estimate at the centre of the view: the projections stretch the
  source differently across it.
- The panel's text layout (fonts, rows, columns) is the player's; `gb-render`'s overlay layout
  (phase 4) may differ, and a still and an export of the same settings may then not match
  exactly in the panel.
- The capture folder is remembered with `QSettings`. The canvas, aspect ratio and rectangle are
  kept for the session only.
