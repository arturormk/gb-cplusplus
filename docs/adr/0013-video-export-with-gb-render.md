# ADR-0013: Video export with gb-render: settings, overlays, chapters and map tiles

Status: Accepted (2026-10-31). Amends ADR-0009 and BLUEPRINT §9 on fetching map tiles for export.

## Context

The export plan ([`docs/plans/export/`](../plans/export/README.md)) makes video export a
command-line tool, `gb-render`, described by `gb.export/1` INI settings, with Capture mode in
`gb-player` (ADR-0012) choosing the framing. This record covers the first video export: the
settings model, the overlays, ranges across chapters, the encoder and the map tiles.

The plan proposed overlays as `[map]`, `[speed]`, `[height]` and `[telemetry]` sections with
`placement = over | beside`. In use, the asks were more specific: the side panel placements of
Capture mode plus `top`, an overlay mode where each telemetry value has its own place and label,
two text colours with an outline so text stays readable on a bright picture, and a map with a fixed
ground width, an optional track over the whole range and an optional circular shape.

## Decision

- **Placement** is one key, `[layout] placement = none | overlay | left | right | top | bottom`.
  The `top` panel is added to the framing geometry and to Capture mode. Capture mode now writes
  `placement = right` instead of `placement = beside` with `panel = right`; the earlier form, and
  `[map] scale` for `[map] width`, are still read.
- **Telemetry text** is `[telemetry] fields` (`datetime`, `date`, `time`, `latitude`, `longitude`,
  `altitude`, `speed`, `distance`), one optional `[field-NAME]` section per field (`label`,
  `anchor`, `align`, `font_size`), and a `[text]` style shared by all fields: value font size as a
  fraction of the frame height, label size relative to it, labels above, left or none, label and
  value colours, and an outline (colour and width). Over the picture each field is anchored at a
  corner or the middle of the top or bottom edge, stacked with the others at the same corner in
  list order, or placed at a free point in fractions of the frame. On a panel the fields are
  listed and shrink to fit. Values come from the recorded record nearest the frame, valid within
  one record interval, or are `—` (ADR-0004, ADR-0009). `distance` is measured from the start of
  the range, ignoring steps under 3 m.
- **The map** is `[map] width` (a ground distance with a unit), `track`, `shape = square | circle`,
  and over the picture `size`, `anchor` and `opacity`. It is always centred on the current
  position, north up. The track is the recorded route between the start and end of the range,
  across chapters. Tiles are drawn at frame height / 1080 output pixels per tile pixel. The
  attribution is always drawn, inside the circle for a round map. The automatic scale stays
  planned.
- **Validation** is strict, as the plan requires: an unknown section or key, a value of the wrong
  form or out of range, or a crop that does not fit is an error naming the file, line and key.
  `--print-settings` writes the combined settings so that reading them back gives the same
  settings. `#` starts a comment only at the start of a line or before whitespace, so colours can
  be written `#ffd43b`.
- **Chapters** are joined by `gb::media::RecordingTimeline`: each chapter starts where the
  previous chapter's video ends (the measured rule), in the first chapter's video time base, and
  chapters of different layout, size or time base are refused. The export reads one chapter at a
  time. Audio of each chapter is placed at the chapter's start on the timeline, padding or
  trimming the few samples by which a chapter's audio differs from its video, and is not padded
  after the recording's last sample.
- **Encoding** is `gb::media::VideoWriter`: libx264 or libx265, yuv420p, BT.709 limited range and
  tagged so, AAC audio, MP4 with `+faststart` or MKV. Nothing is copied from the source: no GPS,
  location atom or camera metadata. Frames are rendered offscreen with the player's shader
  (ADR-0012), composed with QPainter, and encoded in order with their recording PTS rebased to
  the first frame. With `[output] speed` above 1 the export runs faster than real time: frames
  are dropped on a timestamp grid at the source frame rate, and the audio is time-stretched at
  its pitch with libavfilter's `atempo` up to 4×, with no audio track above that (amendment,
  2026-10-31).
- **Map tiles for an export are fetched**, which amends ADR-0009 ("M6 export … with OSM must use
  only tiles already cached") and BLUEPRINT §9 ("never bulk-fetch official OSM tiles for rendering
  a video"), as the export plan decided. The fetch is the narrowest that gives a complete map:
  exactly the tiles the map draws over the range at its single zoom level, computed before
  rendering, no margin and no other zoom levels; cached tiles first; through the same
  `TileFetcher` (two requests at a time, the `gb-render` User-Agent, HTTP caching, back-off and
  the stop on 403/418/429); and never more than `--max-tiles` (default 300) missing tiles, above
  which the export stops before any request. `--map-no-network` is cache-only. The report states
  that fetching reveals the route's area to the tile server.

## Consequences

- An export is reproducible from its settings: the overlays, the course arrow's smoothing (over
  media time) and the map centre depend only on the frame sequence.
- A long export with a narrow map can need more tiles than the cap. The remedies are a wider map,
  a shorter range, an explicit `--max-tiles`, or viewing the route in `gb-player` first. Prepared
  tile directories (`--prepare-tiles`, `--map-tiles`) remain planned.
- The OSM tile usage policy discourages bulk downloading. A capped fetch of the tiles one export
  shows is a judgement that this is interactive-scale use; if the policy or the provider says
  otherwise, `--map-no-network` and other providers remain, and the default can change.
- Throughput on the development machine (Intel UHD 630, Debug build): about 38 frames per second
  for a 1080p H.264 recording with overlays and the `ultrafast` preset, and about 10 with
  `medium`, where the encoder dominates. Reading the GPS of a 4 GB chapter takes several seconds;
  chapters are read in parallel.
- Capture mode's panel text and `gb-render`'s panel text are drawn by different painters, so a
  capture and an export of the same panel settings can differ in the text.
- Not yet done: stills and contact sheets in `gb-render`, the automatic map scale, prepared tile
  directories, graphs, `--show-layout`, `--save-settings`, `--manifest`, `--json`.
