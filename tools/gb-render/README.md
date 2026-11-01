# gb-render

`gb-render` exports a framed video from a GoPro recording, with telemetry text and a route map
drawn over the picture or on a black panel beside it. Every export is described by `gb.export/1`
settings files and options, so it can be repeated exactly. The design is in
[`docs/plans/export/`](../../docs/plans/export/README.md) and
[ADR-0013](../../docs/adr/0013-video-export-with-gb-render.md).

```sh
gb-render --example-settings > ride.ini          # a commented settings file to start from
gb-render GH010073.MP4 --settings framing.ini --settings ride.ini --dry-run
gb-render GH010073.MP4 --settings framing.ini --settings ride.ini --duration 10 --draft -o test.mp4
gb-render GH010073.MP4 --settings framing.ini --settings ride.ini -o ride.mp4
```

`framing.ini` is what `gb-player`'s Capture mode copies or saves: the range, the view and the
capture rectangle. `ride.ini` describes the overlays and the output. Later files and options win,
key by key; `--set SECTION.KEY=VALUE` reaches any setting (`--set map.width=2km`).
`--print-settings` prints the combined result with every key written out.

## Range and chapters

`RECORDING` is any chapter of a recording. `--start` and `--end` (or `--duration`) take positions
as the player's clock shows them: `c2:00:05:31.400` is chapter 2 at 5 min 31.4 s into that
chapter; `GH020073.MP4:5:31.4` names the chapter's file; a time without a chapter is recording
time. A range may cross chapters: they are joined on one timeline, each starting where the
previous one's video ends, with the audio of each chapter placed at its start. The output has one
frame per source frame, at the source's frame rate, starting at zero.

## Placement

`[layout] placement` is one of:

| Value | Overlays |
|---|---|
| `none` | none: the picture only |
| `overlay` | over the picture, at anchored positions (the default) |
| `left`, `right`, `top`, `bottom` | on a black panel beside the picture, `panel_size` frame heights wide (left, right) or tall (top, bottom) |

`[frame] aspect` is the whole output frame, panel included; the picture takes the rest. Sizes are
fractions of the frame height, so a `--draft` and a 4K export have the same layout.

## Telemetry text

```ini
[telemetry]
fields = time, speed, altitude, distance

[text]
font_size     = 0.04     ; value text, fraction of the frame height
label_size    = 0.6      ; labels, relative to font_size
labels        = above    ; above, left (on the value's line) or none
label_color   = #c8d0d8
value_color   = #ffffff
outline       = yes      ; a border around the glyphs, readable on a bright sky
outline_color = #000000
outline_width = 0.08     ; fraction of the text size

[field-speed]
label     = Velocidad    ; any text; empty for no label
anchor    = bottom-left
font_size = 0.08         ; this field only
small     = no           ; yes: the value at half size, about as large as a label

[date]
format   = weekday, month day, year      ; default YYYY-MM-DD
months   = Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec
weekdays = Mon, Tue, Wed, Thu, Fri, Sat, Sun
```

Fields: `datetime`, `date`, `time` (from the GPS UTC time, in `[units] time_zone`), `latitude`,
`longitude`, `altitude`, `speed` (2D), `distance` (travelled since the start of the range,
ignoring movements under 3 m). Units: `[units] speed = km/h | mph | m/s | kn`, `altitude = m | ft`,
`distance = km | mi`, `time_zone = local | utc | +02:00 | Europe/Madrid`.

**Dates.** `[date] format` sets how `date` and `datetime` write the date (`datetime` adds
`HH:MM:SS`). Codes: `YYYY` (2026), `YY` (26), `MM` (03), `M` (3), `DD` (07), `D` (7), which may run
together (`YYYYMMDD`); words: `year` (as `YYYY`), `day` (as `D`), `month` and `weekday`, the names
from `months` and `weekdays`. The names come only from the settings, so any language works:
`months` lists twelve, **starting at January**, and `weekdays` seven, **starting at Monday**
(ISO 8601). They are needed only when the format uses `month` or `weekday`. Other characters are
written as they are, except ASCII letters, which go in quotes: `weekday D 'de' month 'de' YYYY`
(`''` is an apostrophe).

Values follow the player's rules: the recorded GPS record nearest to the frame, with a valid fix
within one record interval of it; otherwise `—`, never zero or an interpolated value.

**Over the picture**, each field goes to its `anchor`: `top-left`, `top`, `top-right`,
`bottom-left` (the default), `bottom`, `bottom-right`, inset by `[layout] margin`, or `x, y` as
fractions of the frame's width and height for the field's top-left corner (`align = right` or
`center` makes `x` its right edge or centre). Fields at the same corner are stacked in the order of
`fields`, away from the map when it shares the corner. **On a panel**, fields are listed in order
below or beside the map; `font_size` is the largest size, and the text shrinks to fit.

Colours are `#rgb`, `#rrggbb`, `#rrggbbaa` or a name (black, white, gray, red, orange, yellow,
green, cyan, blue, magenta). In settings files `;` starts a comment anywhere, and `#` only at the
start of a line or when followed by a space, so `#ffd43b` is a value.

## Map

```ini
[map]
width   = 1.2km      ; ground distance across the map: 800m, 1.2km, 0.5mi, 2000ft, or auto
ahead   = 1min       ; width = auto: the route of the next minute stays in view (1.5, 1.5min, 90s)
closest = 150m       ; width = auto: the narrowest the map gets
widest  = 100km      ; width = auto: the widest
track   = yes        ; the route over the whole range, even across chapters
shape   = circle     ; square, or a circle inscribed in the square
size    = 0.3        ; over the picture: side of the map, fraction of the frame height
anchor  = top-right  ; over the picture, as for fields
opacity = 0.9        ; over the picture
```

The map is always centred on the current position, north up. With `track = yes` it shows the
recorded route from the start to the end of the range, travelled part in orange and the rest in
blue, broken where there is no valid fix; with `no`, only the current position. Without a valid
fix the map stays on the last position and the marker is not drawn. The attribution is always
drawn.

**Automatic scale.** With `width = auto`, the map is as wide as it needs to be for the route of
the next `ahead` (a plain number is minutes) to stay in view, the farthest point of it at three
quarters of the map's radius, within `closest` and `widest`. The scale is computed once, before
rendering, from the GPS records, including those after the end of the range, and is smoothed so
it changes steadily: the map starts zooming out before it is needed (at most a quarter of a zoom
level per second), zooms back in slowly (at most 0.03 levels per second), ignores dips shorter
than 15 s, such as a short stop, and does not follow changes under a tenth of a level. Across a
GPS gap the first fix after it is taken as steady travel, so a gap does not widen the map; where
there is no current fix, or the track ends within the look-ahead, the scale is held. `--dry-run`
reports the closest and widest scale and when they occur. An automatic scale draws tiles at
several zoom levels, so it needs more tiles than a fixed one.

**Tile quality.** Tiles are composed at their own resolution and resampled with an area filter,
so a small frame (where tiles are shrunk) shows no jagged or shimmering lines and text. Within
0.12 of a half zoom level the two nearest tile levels are crossfaded, so a changing scale never
switches them abruptly; such frames need the tiles of both levels.

**Tiles.** Before rendering, `gb-render` works out every tile the map draws over the range, uses
those in the tile cache, and fetches the missing ones through the player's rate-limited fetcher
(two requests at a time, the project User-Agent), at most `--max-tiles` (300) of them. Tiles are
drawn at frame height / 1080 output pixels per tile pixel, so the set is the same at any
resolution. `--map-no-network` uses the cache only; missing tiles are drawn as placeholders and
reported, or are an error with `--require-tiles`. Fetching reveals the route's area to the tile
server, never the recording or its track. `--dry-run` reports the tile count and zoom levels.

## Framing

`[view]` and `[frame]` come from Capture mode. Without them, the export uses the recording's own
picture: a flat view of a flat recording, filling the frame by cropping the middle of the source to
the picture's shape (reported by `--dry-run`).

## Output

MP4 or MKV, H.264 (`libx264`) or HEVC (`libx265`, tagged `hvc1` in MP4), `crf` and
`encoder_preset`, BT.709 limited range. Audio is the recording's mono or stereo track, re-encoded
to AAC (`--no-audio` for none). The video carries no metadata from the recording: no GPS track,
no location atom, no camera serial numbers. The file is written as `NAME.partial.mp4` and renamed
when complete; Ctrl+C removes it. An existing output is not replaced without `--overwrite`.

`--draft` renders at half the width with the fastest encoder preset. Progress goes to stderr.
Exit status: 0 done, 1 failure, 2 usage or settings error, 130 cancelled.

## Not yet

Stills and contact sheets (`--still`, `--contact-sheet`), speed and height graphs,
`--show-layout`, `--save-settings`, `--manifest` and `--json` are planned in
[`docs/plans/export/phases.md`](../../docs/plans/export/phases.md).

Prepared tile directories (`--prepare-tiles`, `--map-tiles`) are not planned. Tiles fetched by an
export stay in the tile cache, so after one run with the network, `--map-no-network
--require-tiles` renders the same export offline. Fetching tiles ahead of time may come back with
support for other map providers.
