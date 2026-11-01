# Export settings file (`gb.export/1`, INI)

`gb-render` reads its settings from INI files: one section per part of the export, one per
overlay. BLUEPRINT §9 asks for a versioned, documented settings format, and suggests JSON "or
another explicitly documented format". INI was chosen over JSON and YAML because:

- people write these files by hand, and INI is the easiest of the three to write, with comments;
- every setting is flat within its section, so INI's lack of nesting costs nothing, and a section
  plus a key is also the path for `--set` (`--set map.opacity=0.7`);
- it needs no dependency. YAML would add yaml-cpp. The parser is small, bounded and lives in the
  Qt-free `gb_export` core, where it is unit-tested like the format libraries.

## Example

`gb-render --example-settings` writes a file like this, with every setting commented. The sections
in use are implemented (ADR-0013), with the automatic map scale (`[map] width = auto`) and the
`[speed]` and `[height]` graphs.

```ini
; gb-render export settings
schema = gb.export/1

[range]
; chapter:time positions, as gb-player shows them (or recording time without a chapter)
start = c1:00:11:30.000
end   = c2:00:02:05.000

[view]
mode   = rectilinear    ; the gb-player view names
yaw    = -12.5
pitch  = 4.0
roll   = 0.0
fov    = 95
; flat views: zoom = 1.0 and pan = 0, 0, as in gb-player
canvas = 1.7778         ; aspect ratio of the view the crop refers to

[frame]
aspect = 16:9           ; 16:9, 4:3, 1:1, 9:16 or W:H; the whole output frame
crop   = 0.12, 0.08, 0.64, 0.64   ; x, y, w, h of the canvas, 0..1; default: all of it
width  = 1920           ; height follows from the aspect ratio

[layout]
placement  = overlay    ; none, overlay, or a black panel: left, right, top, bottom
panel_size = 0.32       ; panel only: fraction of the frame height
margin     = 0.03       ; overlay only: space between overlays and the frame edge

[units]
speed     = km/h        ; km/h, mph, m/s, kn
altitude  = m           ; m, ft
distance  = km          ; km, mi
time_zone = local       ; utc, local, +02:00 or an IANA name such as Europe/Madrid

[text]
font_size     = 0.04    ; value text; on a panel, the largest size
label_size    = 0.6     ; labels, relative to font_size
labels        = above   ; above, left or none
label_color   = #c8d0d8
value_color   = #ffffff
outline       = yes     ; a border around the glyphs
outline_color = #000000
outline_width = 0.08    ; fraction of the text size

[telemetry]
fields = time, speed, altitude, distance

[field-speed]           ; optional, one per field
label     = Speed       ; empty for no label
anchor    = bottom-left ; a corner, top, bottom, or x, y in fractions of the frame
align     = auto        ; auto, left, center, right
font_size = 0.07        ; this field only
small     = no          ; yes: the value at half size, about as large as a label

[date]
format   = YYYY-MM-DD   ; codes YYYY YY MM M DD D, words year month day weekday, 'quoted text'
months   = Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec   ; January first
weekdays = Mon, Tue, Wed, Thu, Fri, Sat, Sun                            ; Monday first

[map]
width   = 1.2km         ; ground distance across the map, or auto
ahead   = 1min          ; auto: the route of the next minute stays in view
closest = 150m          ; auto: narrowest
widest  = 100km         ; auto: widest
track   = yes           ; the route over the whole range, across chapters
shape   = circle        ; square or circle
size    = 0.3           ; overlay only: fraction of the frame height
anchor  = top-right     ; overlay only
opacity = 0.9           ; overlay only

[speed]                 ; also [height]; shown when the section is present
anchor = bottom-right   ; overlay only
margin = 0.03           ; overlay only: inset from the edges, or x, y; default [layout] margin
size   = 0.6, 0.18      ; overlay only: width, height
smooth = 3s             ; centred mean, or no
window = range          ; or a time (2min) that scrolls with the cursor at the centre
color  = #3b9bdb
cursor_color = #f76707
background   = #0d141ab3

[output]
codec  = h264           ; h264 or hevc
crf    = 20
encoder_preset = medium
audio  = yes
audio_bitrate = 192000
speed  = 1              ; times real time, 1..1000; audio keeps its pitch up to 4, none above
```

## Rules

### Syntax

- UTF-8. `key = value` lines, `[section]` headers, comments from `;` to the end of the line, or
  from a `#` at the start of a line or followed by a space or the end of the line, so that colours
  such as `#ffd43b` are values. Keys before the first section are file-wide (`schema`). Blank
  lines are ignored.
- Values are text. Lists are comma-separated. Booleans are `yes`/`no` (also `true`/`false`).
  Colours are `#rgb`, `#rrggbb`, `#rrggbbaa` or a name (black, white, gray, red, orange, yellow,
  green, cyan, blue, magenta).
  Distances take a unit (`m`, `km`, `ft`, `mi`) and durations one (`s`, `min`), so a value never
  depends on a unit setting elsewhere.
- Numbers are parsed in the C locale, as the other tools do, so `0.85` means the same everywhere.
- Lines are limited to 4 KiB and files to 1 MiB. Anything longer is an error, as the format
  libraries do with oversized input.

### Validation

- `schema = gb.export/1` is required. A file without it, or with another version, is rejected.
- Unknown sections and keys are errors that name the file, line and key, so a misspelling is never
  silently ignored. A key given twice in one section is an error.
- Values out of range are errors, with the allowed range.
- A setting that cannot apply is an error: a crop outside the canvas, a view the source
  layout does not support, audio on a recording without a playable track, `--size` not matching
  `aspect`.

### Combining files and options

Built-in defaults, then each `--settings` file in order, then command-line options. A later value
replaces an earlier one key by key. An overlay is present when its section is present in any file
(for the text: `[telemetry]` or any `[field-NAME]`). `[map] enabled = no` or
`[telemetry] enabled = no` removes one that an earlier file added. gb-player 0.1 wrote
`placement = beside` with `panel = SIDE`, and the map width as `[map] scale`; both are still read. Options map onto keys: `--start` is
`range.start`, `--view` is `view.mode`, `--codec` is `output.codec`, and `--set SECTION.KEY=VALUE`
reaches any key.

`--print-settings` writes the combined result in this format, every key explicit, with the range
and view included and the version of `gb-render` in a comment.

### Units and sizes

- Every overlay `size`, font size, `layout.margin` and `layout.panel_size` is a fraction of the
  output frame **height**. Map tiles are drawn at frame height / 1080 output pixels per tile
  pixel. Nothing is in pixels, so a `--draft` export and a 4K export have the same layout, and the
  same settings work at any resolution.
- `anchor` is `top-left`, `top`, `top-right`, `bottom-left`, `bottom` or `bottom-right`, inset by
  the margin, or `x, y` in fractions of the frame's width and height for the top-left corner of
  the overlay (a field's `align = right` or `center` makes `x` its right edge or centre). Fields
  at the same corner are stacked in the order of `[telemetry] fields`, beyond the map when it
  shares the corner.
- On a panel, anchors are ignored: the map is a square at the top of a side panel or at the left
  of a top or bottom panel, and the fields are listed beside or below it, shrinking to fit. The
  graphs come last (below a side panel's text, right of a top or bottom panel's), speed above
  height, splitting the height of their region.
- Over the picture, each graph has its own `anchor`, `margin` (one inset, or `x, y`) and `size`
  (`width, height`). Height is stacked beyond speed when they share a corner; graphs are not
  stacked with the map or fields.
- `[date] format` applies to the `date` field and the date of `datetime`. Month and weekday names
  are never built into the program: `months` (twelve, January first) and `weekdays` (seven, Monday
  first, as ISO 8601) are required when the format uses `month` or `weekday`.
- `[field-NAME] small = yes` draws the value at half its size, about as large as a label.
- The fields shown are `[telemetry] fields`, followed by any field that only has a `[field-NAME]`
  section. Without a list or field sections, they are date and time, latitude, longitude,
  altitude and speed.
- The `[frame] aspect` is the aspect ratio of the whole output frame, including any panel. The
  picture area is what remains.
- `[view] canvas` is the aspect ratio of the view as `gb-player` drew it, and `[frame] crop` the
  part of it that is exported ([capture](capture.md#what-the-rectangle-means)). The crop, measured
  in canvas pixels (`w · canvas` by `h`), must have the picture area's aspect ratio within 0.1 %.
  Without `crop`, the whole canvas is exported, and `canvas` defaults to the picture area's
  aspect ratio.

### What has no setting

- **Map attribution** is always drawn when the map is present.
- **Location in videos.** Export never embeds telemetry or location in a video, so there is no
  setting for it. GPS EXIF in stills is a Capture-mode option, off by default.
- **Map orientation and centring.** The map is always centred on the current position, north up.

### Versioning

A change that alters the meaning of a key, or removes one, makes `gb.export/2`. Adding a key whose
default keeps the output of older files unchanged stays `gb.export/1`. Reserved for later: a
`[keyframes]` section for framing that changes over time.
