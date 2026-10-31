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
  stays a separate file: timeline, telemetry and map cover only the open one (GPS track export
  covers them all).
- **Photos**: MAX JPEG photos open as a still image, recognised by content rather than file
  name. Equirectangular photos, which carry an XMP GPano crop, get the spherical views: the 360°
  `GS__nnnn.JPG` and the superwide `GP__nnnn.JPG`, which is a 270°×90° crop of the 360° canvas.
  Outside the crop the view shows the background. Other photos, such as the single-lens wide
  `GP__nnnn.JPG`, are shown flat with zoom and pan. There is no transport, timeline, height/speed
  graph or audio. See [Photos](#photos) and ADR-0011.

## Use

```sh
gb-player recording.360
gb-player --time 120 --view panorama recording.360
gb-player --play --decoder-threads 4 recording.360
gb-player --benchmark 60 --seeks 20 --time 60 recording.360 > benchmark.json
gb-player --screenshot view.png --time 5 recording.360
gb-player --map-no-network recording.360        # cached map tiles only
gb-player --map-cache /tmp/tiles recording.360  # use another tile cache
gb-player --view panorama GS__0033.JPG           # a 360° photo
```

| Input | Action |
|---|---|
| Space, double-click | play / pause |
| Left / Right | seek −5 s / +5 s (in Capture mode they move the capture rectangle) |
| `,` / `.` or `Ctrl+←` / `Ctrl+→` | step one frame back / forward (pauses) |
| Drag, wheel | look around and change FOV (spherical modes); pan and zoom (2D modes) |
| 1–8 | rectilinear, fisheye, stereographic, Little Planet, panorama, raw 1, raw 2, original |
| Timeline press and drag | scrub: frames follow the pointer, clock held, audio silent until release + 250 ms |
| M | mute / unmute (the audio clock keeps running) |
| R / F / Ctrl+O | reset view / full screen / open file |
| S, toolbar Square | square view, so the fisheye-family image circle is round |
| Toolbar chapter drop-down | open another chapter of the recording |
| Map: drag, wheel, `+` / `−` | pan (stops following), zoom |
| Map: click on the route | seek there; if the route passes several times, choose the time |
| Map: `⤢` / `◎` | fit the whole route / follow the current position |

For a recording split into chapters, the clock shows the chapter in front of the time within
that chapter's file: `c2:00:05:31.400` is chapter 2 at 5 min 31.4 s.

Seeking is accurate. The displayed frame is the latest frame whose presentation time is not after
the requested time. Frame stepping and the timeline both use the first stream's integer ticks.

## Capture mode

The toolbar's **Play | Capture** selector switches to Capture mode, for framing and capturing
stills from photos and video frames. Aim the view in Play mode first; aiming still works in
Capture mode.

- **The canvas** is the view as the player draws it. Its aspect ratio is frozen when Capture mode
  is first entered, and the view is letterboxed to it, so resizing the window never changes what
  is in it. With Square on, the canvas is square.
- **The capture rectangle** selects part of the canvas, at the frame's aspect ratio: 16:9, 4:3,
  1:1, 9:16 or a free W:H. Outside it the view is dimmed. Drag inside it to move it and its
  corner handles to resize it; its aspect ratio stays locked. Dragging outside it aims the view,
  and the wheel changes the field of view (or zooms), as in Play mode.
- **Panel**: none, left, right or bottom, with its size as a percentage of the frame height. It
  takes part of the output frame, so the rectangle gets the aspect ratio of what is left. It is
  black and shows the route map (at the map pane's zoom, centred on the position, north up, with
  the attribution; the **Track** checkbox draws the route with its start and end, or only the
  position — for stills only, not in the gb-render settings) and the date and time (local,
  from GPS), latitude, longitude, altitude and speed at the frame, or "—" without a valid fix
  near it. The preview next to the rectangle is drawn as the still will be. When the window is
  not wide (or tall) enough for it, the canvas gets room on the panel's side and scroll bars,
  which appear when the pointer is over the bottom or right edge. Before writing, the map's
  missing tiles are requested through the map's rate-limited fetcher; Capture waits up to
  five seconds, and tiles that do not arrive are drawn as placeholders and reported.
- **Capture** renders the displayed frame (pausing playback first) at the chosen size, from the
  source, as JPEG or PNG. Every output pixel is computed by the player's shader, not scaled up
  from the screen. The name is suggested from the recording, position and view, for example
  `GH020073_c2-05m31s400_rectilinear.jpg`, in the capture folder; an existing file is never
  replaced. Shift+click asks for a name. The status line reports the file and any warning, such as
  an output with more pixels than the source can resolve.
- **Include GPS position (EXIF)**, off by default, writes the position into a JPEG: for a video
  frame, the nearest recorded GPS record, only if it has a valid fix within one record interval of
  the frame; for a photo, its own position. It is disabled, with the reason, when there is none.
  Without it a still carries no EXIF at all.
- **Range**: the current position as a chapter position (`c2:00:05:31.400`), and *Set start* /
  *Set end*, which only go into the settings.
- **Settings**: the framing as `gb.export/1` INI text (`[range]`, `[view]`, `[frame]`, and with a
  panel `[layout]`, `[units]`, `[map] scale` and `[telemetry]`), updated live, with **Copy** and **Save as…**. They are meant for a `gb-render`
  video export (see [`docs/plans/export/`](../../docs/plans/export/README.md); `gb-render` itself
  is not implemented yet).

| Input (Capture mode) | Action |
|---|---|
| Drag inside the rectangle / its corners | move / resize it |
| Arrow keys, Shift+arrow keys | nudge it by one / ten screen pixels |
| Ctrl+wheel | scale it about its centre |
| Shift+click Capture | choose the file name |

Switching back to Play restores the map and panels. The view, the aspect ratio and the rectangle
are kept for the session. See ADR-0012.

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
  Both graphs are smoothed: each value is the mean of the records within a centred 3-second
  window, never reaching across a break in the line. The jagged-line button left of the export
  button shows the raw records instead. The value axis stays the same, so the raw and smoothed
  curves can be compared.
  The square button at the right of the Height/Speed tabs exports the GPS track as GPX, GeoJSON,
  CSV or JSON, the formats of `gb-telemetry`: pick the file type and name in the save dialog. For
  a chaptered recording the export always covers every chapter, in chapter order, in one file; see
  [`gb-telemetry`](../../tools/gb-telemetry/README.md) for how chapters are represented. GPX and
  GeoJSON omit records without a valid fix. The export runs in the background and its result is
  shown in the status line.
- **Metadata**: file and stream properties, the ISO BMFF track table, and the GPMF tree of the
  `gpmd` sample covering the playback time. Unknown keys are flagged.
- **Performance**: displayed, presented and decoded rates, dropped frames (late or not drawn),
  decoder underruns, queue depth, unpaired frames, seek latency, texture upload time, GPU time
  (OpenGL timer queries), process CPU, RSS, and the OpenGL renderer. It also shows the audio state
  and why it is silent (paused, scrubbing, seeking, or settling), the output driver, buffer and
  latency estimate, audio underruns, and alignment counters.

The timeline marks `gpmd` sample positions, GPS coverage, and reported telemetry gaps.

## Photos

For a photo, the panels show what the file records about its capture:

- **Telemetry**: the EXIF GPS position (UTC, latitude, longitude, altitude, and DOP and speed
  when present). It is a single position, with no track. Then **Camera**: the field-of-view class
  and the evidence for it, image size, capture time from the camera clock (EXIF has no time
  zone), camera and firmware, shutter speed, aperture, ISO, exposure bias, program, metering, white
  balance and focal length. GoPro settings from the APP6 segment follow: resolution, projection,
  field of view, stabilization, auto-ISO range and Protune. A value the photo does not record is
  marked "not recorded". MAX superwide and 360 photos, for example, carry no EXIF shutter speed
  or ISO.
- **Metadata**: file properties, JPEG codestream count (the second is the camera's preview),
  EXIF byte order, the GPano crop, and the full GoPro APP6 GPMF tree.
- **Map**: the photo's position as a single marker, without route, start, end or direction. A
  photo without EXIF GPS shows a message.

The field of view is classified as follows. A GPano equirectangular crop covering the whole
canvas is **360°**; a narrower crop is **superwide**, with its horizontal angle. Otherwise the
GoPro projection (`PRJT`: `ERP0` 360°, `PANO` superwide) decides, and failing that the lens letter
ending the GoPro resolution (`PRES`, for example `5.5MP_W`): **wide**, **linear** or **narrow**.
The MAX's "normal" photo mode records the wide lens. Only `W` has been seen in a real file; `L`
and `N` are inferred.

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
paused; any violation also makes the run fail. With `--stall SECONDS`, it then plays from the
start and blocks the GUI thread for that long, as a throttled buffer swap does. The `stall` section
reports how many frame sets the decode worker decoded and dropped meanwhile, and the run fails if
it decoded fewer than 80% of the sets due. Seek latency runs from
the request to the first drawn frame. CPU is process CPU time over wall time. GPU time measures
texture upload plus drawing on the GPU. The `map` section reports how the drawn map marker
compares with the GPS record nearest to the displayed PTS. The record is sampled about every
100 ms while playing, where up to two frames of lag is allowed. After the pause and after every
seek it must match exactly. The section also gives tile cache and request counts. The report
contains no telemetry values. A benchmark never continues into the next chapter. Results describe
the machine they were measured on; see `research/findings/` for recorded runs.

## Timing trace

Set `GB_PLAYER_TRACE=1` to print one line a second to standard error while a recording is open:

```sh
GB_PLAYER_TRACE=1 gb-player --play recording.360 2> trace.log
```

Each line shows whether playback and audio are active, how often the GUI thread ran
(`frame_ticks` from the scene graph, `timer_ticks` from the engine timer, `max_gap_ms` for the
longest wait between them), the frame sets presented and dropped as late, the decode queue depth,
and how far the presented picture is behind the playback clock (`video_behind_ms`). It is meant
for diagnosing playback that falls behind its audio; see
`docs/incidents/2026-10-31-video-lag-after-screen-lock.md`.

## Requirements and limitations

- Requires an OpenGL 3.3 (or OpenGL ES 3.0) Qt Quick scene graph for the picture. With the
  software backend, decoding, presentation and telemetry still work, but no image is shown.
- Audio: the first mono or stereo stream plays through SDL2 (`--no-audio` disables it). The MAX
  four-channel spatial track is not rendered. Hardware decoding (VA-API), edit lists,
  a joined timeline across chapters, and video export are not implemented.
- Capture mode needs an OpenGL 3.3 (or OpenGL ES 3.0) context for offscreen rendering. Captures
  have no overlays; those are for `gb-render`.
- Photos: only the first JPEG codestream is shown; other MPF entries are ignored. The APP6
  camera orientation is listed but not applied to the view. Linear and narrow lens
  classification is unverified (ADR-0011).
- The map is raster only, and tiles can look soft on HiDPI screens. Routes crossing the
  antimeridian are not drawn across it. The tile cache is never pruned automatically.
- MAX reconstruction requires the verified two-stream geometry. The near-black `BACK` face of the
  fingerprinted sample is shown as recorded.
- Fisheye-family views keep the shared reference convention of normalizing each axis separately,
  so non-square views are elliptical unless the Square toggle is on.
