# Capture mode in `gb-player`

`gb-player` gets a **Capture** mode for framing and capturing stills, from photos and from video
frames. Its sidebar shows the framing as `gb-render` settings, ready to copy, so the same framing
can be used for a video export on the command line ([cli.md](cli.md)). This is the only export
GUI. Overlays over the picture are still described in settings files and rendered by
`gb-render`; a side panel can carry the route map and the telemetry text
([side panels](#side-panels)).

## Workflow

1. In Play mode, as now: open a recording, find the moment, choose the projection and aim it.
2. Switch the toolbar's **Play | Capture** selector to *Capture*.
3. Choose an **aspect ratio**: 16:9, 4:3, 1:1, 9:16 or free (W:H).
4. Drag the **capture rectangle** to move it, and its corner handles to resize it. Its aspect
   ratio stays locked. Outside the rectangle the view is dimmed.
5. Click **Capture**. The displayed frame is written as a still at the chosen resolution.
6. Copy the **settings** from the sidebar, or save them to a file, and use them with
   `gb-render --settings` for a video export. *Set start* and *Set end* add the current position
   as the range for that export.

Switching back to Play restores the Play mode sidebar. The view, the aspect ratio and the
rectangle are kept for the session.

## What the rectangle means

The rectangle selects part of the **canvas**: the view as `gb-player` draws it, with its
projection, angles, field of view, zoom and pan. The export renders exactly that part of the
canvas at the output resolution. The output is not an upscaled screenshot: the renderer computes
every output pixel from the source frame, as the player does for the screen.

This means the same thing for every view:

- **Original, raw tracks and Panorama**: a rectangle of the flat picture.
- **Rectilinear**: a rectangle of the rectilinear view. An off-centre rectangle is a rectilinear
  picture with a shifted centre, as a shift lens gives. Straight lines stay straight. Aiming the
  view so the subject is in the middle and keeping the rectangle centred gives the usual
  centred perspective.
- **Fisheye, Stereographic, Little Planet**: a rectangle of the projected image, for example the
  middle of a Little Planet.

**The canvas aspect ratio is frozen** when Capture mode is entered. The view is letterboxed to
it, so resizing the window scales the canvas but never changes what is in it, and the rectangle
keeps its place on the picture. With *Square* on, the canvas is square. The frozen aspect ratio
is part of the settings (`canvas`), so `gb-render` reproduces the same canvas without a window.

Aiming still works in Capture mode: dragging outside the rectangle changes yaw and pitch (or
pans a flat view), and the wheel changes the field of view (or zooms), as in Play mode.
Ctrl+wheel scales the rectangle about its centre. The arrow keys nudge it by one screen pixel, or
by ten with Shift.

## Side panels

Overlays placed *beside* the picture take part of the output frame, so the picture's aspect
ratio is not the frame's. The sidebar has a **Panel** choice (none, left, right or bottom, and its
size). The rectangle takes the aspect ratio of what is left. The frame aspect ratio, which is
what upload targets require, stays the one chosen.

The panel is black and can show, each with a checkbox (both on by default):

- **Map**: the route map, a square at the top of a side panel or at the left of a bottom panel. It
  has the map pane's zoom, so its ground scale is what the pane shows, and it is centred on the
  current position, north up, as `gb-render` draws it (on the pane's centre when there is no
  position). Tiles are drawn at frame height / 1080 output pixels per tile pixel, and the
  attribution is always drawn with it. Without a GPS track it says so.
- **Telemetry**: date and time (from the GPS UTC time, in the computer's time zone, with its
  offset), latitude, longitude, altitude and speed. The values come from the same fix as the GPS
  EXIF: the nearest record, only with a valid fix within one record interval of the frame, or a
  photo's own position. Otherwise they show "—". The text shrinks to fit; a wide bottom panel uses
  two columns.

The preview next to the rectangle is drawn by the same painter as the still, scaled down. Before
writing a still with a map, Capture asks the player's tile fetcher for the tiles the map needs
that are not cached, with the same rate limits as the map pane, and waits up to five seconds.
Tiles that do not arrive are drawn as placeholders and reported in the status line. Charts are not
drawn in the panel.

## The sidebar

In Capture mode the right-hand panel (map, telemetry, graphs) is replaced by:

| Group | Contents |
|---|---|
| Frame | Aspect ratio buttons, free W:H fields, panel side and size, and the panel's Map and Telemetry checkboxes. |
| Capture | Resolution presets for the aspect ratio, or a width with the height derived from it. JPEG or PNG, JPEG quality. *Include GPS position (EXIF)*, off by default, disabled with the reason when there is no valid fix near the frame. The **Capture** button, and the folder captures go to. |
| Range | The current position as a [chapter position](cli.md#positions-in-a-chaptered-recording). *Set start* and *Set end*, for the settings only. |
| Settings | The settings as INI text, monospace and read-only, updated live as the view and rectangle change. **Copy** and **Save as…** |

The settings shown are the parts `gb-player` knows about: `[range]`, `[view]`, `[frame]`, and,
when a panel is set, `[layout]` with what the panel shows: `[units]` and `[telemetry]` for the
text, and `[map] scale` (the ground distance across the map) when there is a GPS track. For
example:

```ini
; from gb-player 0.1.52, GH010073.MP4
schema = gb.export/1

[range]
start = c2:00:05:31.400
; end = (Set end in gb-player)

[view]
mode   = rectilinear
yaw    = -12.5
pitch  = 4.0
roll   = 0.0
fov    = 95.0
canvas = 1.7778

[frame]
aspect = 16:9
crop   = 0.1200, 0.0800, 0.6400, 0.6400
width  = 1920
```

It is meant to be combined with an overlay file:
`gb-render GH010073.MP4 --settings framing.ini --settings ride.ini -o ride.mp4`.

## Capturing

- A capture renders the **displayed frame**: the frame set the engine is showing, not a frame
  decoded again. If the video is playing, Capture pauses it first, so the frame captured is the
  one on screen.
- It uses the same renderer and still writer as `gb-render --still`, through the shared
  libraries ([pipeline](pipeline.md#where-the-code-goes)). A test checks that a capture and
  `gb-render --still` with the copied settings give the same image.
- The file name is suggested from the recording, position and view, for example
  `GH010073_c2-00m05s400_rectilinear.jpg`, in the last folder used. Shift+click asks for a name.
  An existing file is never replaced without asking.
- The status line reports the file written, and any warning (no GPS fix, output larger than the
  source can resolve).
- Photos work the same way, with no range.

## What does not change

Play mode is unchanged, apart from the chapter prefix on the clock of a chaptered recording
([positions](cli.md#positions-in-a-chaptered-recording)). Capture mode adds no overlays over the
picture and no video export. The only tiles it fetches are those of a still's panel map.
