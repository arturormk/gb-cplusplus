# gb-player

`gb-player` is the native Qt 6 Quick desktop player (display name configured by the
`GB_PLAYER_DISPLAY_NAME` CMake cache variable, provisionally "GoBro"). It opens original GoPro
recordings read-only and plays them without conversion:

- **MAX dual-stream `.360`**: both HEVC lens tracks are decoded in lockstep and reconstructed on
  the GPU with the face-specific EAC mapping verified in M3. The view can be rectilinear,
  fisheye, stereographic, Little Planet, the full equirectangular panorama, or either raw track.
- **Single-stream video** (for example a MAX "superwide" MP4): shown flat with zoom and pan. No
  projection is inferred from the capture mode.
- **Audio**: the first mono or stereo track (the stereo AAC track on MAX and MP4) plays in sync,
  with the audio device as the master clock. Audio plays only during normal, stable playback.
  It is silent while the timeline is dragged, while a seek is decoding, for 250 ms after each
  seek, and while paused or stepping. Holding an arrow key or scrubbing therefore never produces
  sound fragments. See ADR-0008.
- **Map**: an OpenStreetMap map of the recorded GPS route with the current position, kept in
  step with the displayed frame. Click the route to seek there. See ADR-0009.
- **Chapters**: the camera splits long recordings into files `GSnnmmmm.360` (MAX),
  `GHnnmmmm.MP4` (AVC) or `GXnnmmmm.MP4` (HEVC), where `nn` is the chapter (01 onwards) and
  `mmmm` the recording number, per GoPro's
  [file naming convention](https://community.gopro.com/s/article/GoPro-Camera-File-Naming-Convention).
  When the opened file is one of several chapters in its folder, the toolbar's file name becomes a
  drop-down listing them in order; choosing one opens it from the start, playing if playback was
  running. When playback reaches the end of a chapter, the next one opens and plays. Each chapter
  stays a separate file: timeline, telemetry and map cover only the open one.

## Use

```sh
gb-player recording.360
gb-player --time 120 --view panorama recording.360
gb-player --play --decoder-threads 4 recording.360
gb-player --benchmark 60 --seeks 20 --time 60 recording.360 > benchmark.json
gb-player --screenshot view.png --time 5 recording.360
gb-player --map-no-network recording.360        # cached map tiles only
gb-player --map-cache /tmp/tiles recording.360  # use another tile cache
```

| Input | Action |
|---|---|
| Space, double-click | play / pause |
| Left / Right | seek −5 s / +5 s |
| `,` / `.` or `Ctrl+←` / `Ctrl+→` | step one frame back / forward (pauses) |
| Drag, wheel | look around and change FOV (spherical modes); pan and zoom (2D modes) |
| 1–8 | rectilinear, fisheye, stereographic, Little Planet, panorama, raw 1, raw 2, original |
| Timeline press and drag | scrub: frames follow the pointer, clock held, audio silent until release + 250 ms |
| M | mute / unmute (the audio clock keeps running) |
| R / F / Ctrl+O | reset view / full screen / open file |
| Toolbar chapter drop-down | open another chapter of the recording |
| Map: drag, wheel, `+` / `−` | pan (stops following), zoom |
| Map: click on the route | seek there; if the route passes several times, choose the time |
| Map: `⤢` / `◎` | fit the whole route / follow the current position |

Seeking is accurate. The displayed frame is the latest frame whose presentation time is not after
the requested time. Frame stepping and the timeline both use the first stream's integer ticks.

## Panels

- **Telemetry**: synchronized to the displayed frame. It shows the nearest recorded `GPS5`
  record: GPS UTC, fix, DOP, latitude, longitude,
  WGS 84 altitude, 2D/3D speed, and the azimuth of the 2D speed vector (`0` north, `90` east,
  `180` south, `270` west) with its 16-point compass direction, e.g. `67.5°  ENE`. That azimuth is the direction of travel derived from neighbouring
  distinct positions, not the camera's pointing direction; it is unavailable when stationary
  (2D speed ≤ 0.01 m/s) or when no distinct neighbouring fix exists. Missing data is shown as
  unavailable, values from a record without a valid fix are shown in yellow, and a reported gap
  is named. Positions are never interpolated. Below the values, a graph of height (WGS 84) or 2D speed against media time
  covers the whole recording, with a cursor at the displayed frame; it fills the rest of the
  panel, and its Height/Speed tabs switch between them. It is shown once the recording's GPS
  records have loaded. Records without a valid fix are left out, and the line breaks across
  them. Click or drag on the graph to seek. The mouse wheel zooms the time axis around the
  pointer, from the whole recording down to 30 seconds, and dragging with the middle button pans;
  the view never extends past the start or end of the video.
- **Metadata**: file and stream properties, the ISO BMFF track table, and the GPMF tree of the
  `gpmd` sample covering the playback time. Unknown keys are flagged.
- **Performance**: displayed, presented and decoded rates, dropped frames (late or not drawn),
  decoder underruns, queue depth, unpaired frames, seek latency, texture upload time, GPU time
  (OpenGL timer queries), process CPU, RSS, and the OpenGL renderer. It also shows the audio state
  and why it is silent (paused, scrubbing, seeking, or settling), the output driver, buffer and
  latency estimate, audio underruns, and alignment counters.

The timeline marks `gpmd` sample positions, GPS coverage, and reported telemetry gaps.

## Map

The map pane sits above the panels; drag the divider to resize it. It shows:

- **Route**: one point per recorded `GPS5` record. Only valid fixes are drawn, one line per run
  of valid fixes, so an invalid stretch appears as a break. The part already travelled is
  orange; the rest is blue. A green dot marks the start and a red square the end.
- **Marker**: the same nearest record the Telemetry panel shows. It is never interpolated. The
  arrow is the direction of travel, not the camera heading. If the nearest record is not a valid
  fix, no marker is drawn and the map says so. Inside a reported GPS gap, a hollow dashed ring
  marks the nearest recorded fix, with its time offset. Recordings without GPS show a message and
  make no tile requests.
- **Click-to-seek**: a click within 10 pixels (at least 5 m) of the route seeks to the nearest
  recorded point. When the route passed there more than once, each pass is listed by time, with
  the one nearest the current time marked.
- **Follow mode** (on by default) recentres the map when the marker leaves the middle of the
  view. Dragging turns it off; `◎` turns it back on.

Tiles come from the OpenStreetMap standard tile layer, under its
[tile usage policy](https://operations.osmfoundation.org/policies/tiles/):

- the User-Agent names the application;
- at most two requests run at once;
- only tiles covering the visible area are requested, with no prefetching;
- `Cache-Control`, `Expires` and `ETag` are honoured, with a 7-day fallback, and expired tiles are
  revalidated with conditional requests; and
- the attribution "© OpenStreetMap contributors" stays visible.

If the server refuses service (HTTP 403, 418 or 429), the player stops requesting tiles for the
session and says so. Requests reveal the viewed area to the tile server, but never the recording
or its track. `--map-no-network` shows only cached tiles.

Tiles are cached at `$XDG_CACHE_HOME/gb/tiles/<provider>/<z>/<x>/<y>.png` (default
`~/.cache/gb/tiles`), each with a `.meta` file holding its expiry and validators. The cache has no
size limit and can be deleted at any time. Set the `GB_MAP_CONTACT` CMake cache variable to add a
contact URL or e-mail to the User-Agent. Providers are descriptions (`gb::geo::TileProvider`);
`--map-provider` selects one, and `osm` is currently the only one built in.

## Benchmark report

`--benchmark SECONDS` waits for the first drawn frame and loaded metadata. It then plays from
`--time` for the requested wall-clock duration, pauses, and performs `--seeks` random accurate
seeks (deterministic with `--seed`). Finally it prints a `gb.player.benchmark/v1` JSON report and
exits non-zero if any seek shows a frame that does not cover its target, or if the map is not
aligned. Between playback and the
seeks it checks audio gating: audio must be heard while playing and must resume afterwards. No
audible frame is allowed during a timeline scrub, during a burst of seeks while playing, or while
paused; any violation also makes the run fail. Seek latency runs from
the request to the first drawn frame. CPU is process CPU time over wall time. GPU time measures
texture upload plus drawing on the GPU. The `map` section reports how the drawn map marker
compares with the GPS record nearest to the displayed PTS. The record is sampled about every
100 ms while playing, where up to two frames of lag is allowed. After the pause and after every
seek it must match exactly. The section also gives tile cache and request counts. The report
contains no telemetry values. A benchmark never continues into the next chapter. Results describe
the machine they were measured on; see `research/findings/` for recorded runs.

## Requirements and limitations

- Requires an OpenGL 3.3 (or OpenGL ES 3.0) Qt Quick scene graph for the picture. With the
  software backend, decoding, presentation and telemetry still work, but no image is shown.
- Audio: the first mono or stereo stream plays through SDL2 (`--no-audio` disables it). The MAX
  four-channel spatial track is not rendered. Hardware decoding (VA-API), edit lists,
  a joined timeline across chapters, photos, and export are not implemented.
- The map is raster only, and tiles can look soft on HiDPI screens. Routes crossing the
  antimeridian are not drawn across it. The tile cache is never pruned automatically.
- MAX reconstruction requires the verified two-stream geometry. The near-black `BACK` face of the
  fingerprinted sample is shown as recorded.
- Fisheye-family views keep the shared reference convention of normalizing each axis separately,
  so non-square views are elliptical.
