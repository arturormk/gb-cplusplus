# Changelog

Notable changes per release series. Odd minors are development series whose patch is the Git
commit count; even minors are manually versioned releases (see
[ADR-0010](docs/adr/0010-versioning-and-releases.md)).

## 0.1 — first public development series

First public release: a minimum usable product for GoPro MAX 2025 recordings on Linux.

- `gb-player`: plays original MAX `.360` files directly with GPU spherical viewing, accurate
  seeking, synchronized telemetry and metadata panels, and audio that plays only during stable
  playback. It shows a live OpenStreetMap route map with click-to-seek and has a benchmark mode.
  It opens MAX JPEG photos, recognised by content, with spherical views for 360° and superwide
  photos, their EXIF position on the map, and their capture settings (ADR-0011). A Square toggle
  keeps the fisheye-family image circle round. The height/speed graph is smoothed over a centred
  3-second window, with a toggle for the raw records. Video now stays in step with audio after a
  screen lock, and `GB_PLAYER_TRACE=1` prints a timing trace.
- `gb-player` Capture mode: a Play | Capture selector, a frozen canvas, a capture rectangle at a
  chosen aspect ratio with an optional side panel showing the route map and the telemetry at the
  frame, stills of the displayed frame rendered
  offscreen from the source as JPEG or PNG, opt-in GPS EXIF, and the framing shown as
  `gb.export/1` settings to copy or save (ADR-0012). The clock shows the chapter of a chaptered
  recording (`c2:00:05:31.400`). The player's shader moved into the new `gb_render` library, and
  framing geometry and settings into the Qt-free `gb_export`.
- `gb-render`: video export from settings files (ADR-0013). Placement none, overlay, or a black
  panel left, right, top or bottom; telemetry fields with custom labels, anchors and sizes, two
  text colours and an outline; a fixed-width map, square or round, with the route over the whole
  range; ranges across chapters; H.264/HEVC with AAC in MP4 or MKV, with no source metadata; map
  tiles fetched before rendering up to `--max-tiles`. Capture mode gains a top panel. An
  automatic map scale, `[map] width = auto`, keeps the route of the next `ahead` minutes in view,
  zooming out ahead of time and back in slowly. Export map tiles are area-filtered and crossfaded
  between zoom levels, so small frames and a changing scale stay smooth.
- `gb-render`: `[date]` sets the date format of the date and datetime fields (default
  `YYYY-MM-DD`), with month and weekday names given in the settings (January and Monday first),
  and `[field-NAME] small = yes` draws a value at half size.
- Route map (player, stills and export): implausible GPS jumps break the route. Short clusters
  of fixes isolated by jumps are omitted. Whether a step is a jump depends on the neighbouring
  steps and the reported speed, so walking and flying recordings are judged alike.
- `gb-render`: `[output] speed` exports faster than real time (1–1000×) by dropping frames before
  they are rendered or encoded, with the audio time-stretched at its pitch up to 4× and no audio
  track above that. libavfilter is now a build dependency.
- `gb-inspect`: ISO BMFF container and content-based JPEG inspection.
- `gb-metadata`: GPMF metadata inventory, sample decoding, and raw payload export.
- `gb-telemetry`: timestamped GPS export to JSON, CSV, GPX, and GeoJSON.
- `gb-time`: diagnostics for the relationship between the media and GPS clocks.
- `gb-frames`: native frame extraction and deterministic MAX EAC reconstruction.
- `gb-project`: Qt visual comparison and an interactive virtual camera.
- Licensed under GPL-3.0-or-later. Versioning follows the odd/even scheme of ADR-0010.
