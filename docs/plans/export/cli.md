# Export: the `gb-render` command

Video export is a command-line tool, `gb-render` (the name BLUEPRINT gives the headless renderer).
What is implemented is listed in [`tools/gb-render/README.md`](../../../tools/gb-render/README.md);
options it does not list (`--still`, `--contact-sheet`, `--show-layout`, `--save-settings`,
`--manifest`, `--json`) are still planned. Prepared tile directories (`--prepare-tiles`,
`--map-tiles`) are not planned ([map tiles](#map-tiles)).
The map scale option is `--map-width DISTANCE`, and overlays are placed with `--placement`.
The moment, the view and the capture rectangle are chosen in `gb-player`'s
[Capture mode](capture.md), which also captures stills. Everything else is written down in
settings files and on the command line, so an export can always be repeated exactly.

## Workflow

1. **Frame the export in `gb-player`'s Capture mode** ([capture.md](capture.md)): position,
   projection, aspect ratio and capture rectangle, and *Set start* / *Set end*. Copy or save the
   settings it shows, for example as `framing.ini`.
2. **Describe the overlays in an INI settings file** ([settings.md](settings.md)). Start from
   `gb-render --example-settings > ride.ini`, which writes a commented example.
3. **Check single frames.**

   ```sh
   gb-render GH010073.MP4 --settings framing.ini --settings ride.ini --still c2:00:05:31.400 -o check.jpg
   ```

   A still takes about a second, so the layout can be adjusted by editing the file and running it
   again. `--show-layout` draws each overlay's rectangle and name on the still, to help with
   placement. The result can be opened in `gb-player`, which opens JPEGs, or in any image viewer.
4. **Check the whole range at once.** `--contact-sheet 12` renders 12 frames spread evenly over
   the range into one image, each labelled with its position. This is how to check that an
   automatic map scale behaves well across a ride, without rendering video.
5. **Export a short test segment.**

   ```sh
   gb-render GH010073.MP4 --settings framing.ini --settings ride.ini --duration 10 --draft -o test.mp4
   ```

   `--draft` renders at a quarter of the output area with the fastest encoder preset. Because
   overlay sizes are fractions of the frame height ([settings](settings.md#units-and-sizes)), the
   draft has the same layout as the full export.
6. **Export.** The same command without `--draft`, with the full range. `--dry-run` first reports
   what would be done (snapped range, frame count, output size, map tiles to fetch, warnings)
   without writing anything.

## Command line

```text
gb-render RECORDING [--settings FILE]... [options] -o OUTPUT
```

`RECORDING` is any chapter of the recording, or a photo. Settings are combined in this order,
later ones winning: built-in defaults, then each `--settings` file in order, then options. So
overlay layout can live in one file and output settings in another (`--settings bike.ini
--settings youtube-1080p.ini`).

| Group | Options |
|---|---|
| Range | `--start POS`, `--end POS` or `--duration SECONDS`. Without them, the whole recording. |
| Stills | `--still POS` (repeatable; one file each, numbered when there are several). `--contact-sheet N`. For a photo, `--still` takes no position. |
| View | `--view MODE` (the `gb-player` names), `--yaw`, `--pitch`, `--roll`, `--fov`, and for flat views `--zoom` and `--pan X,Y`. `--canvas ASPECT`: the aspect ratio of the canvas the crop refers to. |
| Frame | `--aspect 16:9` (also `4:3`, `1:1`, `9:16`, `W:H`), `--crop X,Y,W,H` in canvas units (the whole canvas by default), `--width W` (height from the aspect ratio) or `--size WxH` (must match it). |
| Output | `-o FILE`. The container comes from the extension (`.mp4`, `.mkv`, `.jpg`, `.png`). `--codec h264` or `hevc`, `--crf N`, `--encoder-preset NAME`, `--no-audio`, `--audio-bitrate BPS`, `--jpeg-quality Q`, `--gps-exif`, `--overwrite`. |
| Map | `--map-scale 1.2km` or `--map-scale auto`, `--map-activity walk\|hike\|bicycle\|car\|flight`, and the network options `gb-player` has: `--map-provider`, `--map-no-network`, `--map-cache`. Plus `--max-tiles N` and `--require-tiles`. |
| Anything else | `--set SECTION.KEY=VALUE` overrides one setting in the file, for example `--set map.opacity=0.7`. |
| Checking | `--dry-run`, `--draft`, `--show-layout`, `--print-settings` (prints the combined settings file and exits), `--manifest FILE` (per-frame record, see [pipeline](pipeline.md#timing)), `--json` (machine-readable report on stdout). |
| Standard | `--help`, `--version`, and the exit codes and stderr diagnostics of the other tools. |

Only the settings people change from export to export are options. Overlay layout is in the file,
where it can be commented, versioned and shared, and `--set` reaches the rest.

**Repeatability.** `--save-settings FILE` writes the combined settings, including the range and
view given as options, and the command line to rerun them. Running `gb-render RECORDING
--settings FILE -o OUTPUT` gives the same frames again. The file records the tool version.

## Positions in a chaptered recording

Start and end points are written as **chapter:time**, using the clock text `gb-player` shows under
the video:

```text
c2:00:05:31.400      chapter 2, at 00:05:31.400 into that chapter's file
```

- `c` and the chapter number come first. The chapter number is the position in
  `chapter_sequence` order, 1-based, which is also the `nn` in `GHnnmmmm.MP4` / `GSnnmmmm.360`
  when no chapter is missing. The `c` keeps it from being read as hours.
- The time is the position **within that chapter**, in the player's `HH:MM:SS.mmm`. Shorter forms
  are accepted: `c2:5:31.4`, `c2:331.4` (seconds).
- The chapter's file name works too: `GH020073.MP4:00:05:31.400`.
- A time without a chapter is **recording time**, from the start of the first chapter. For a file
  that is not chaptered the two are the same.
- Positions are snapped to frames ([pipeline](pipeline.md#timing)). `--dry-run` and the report
  show the snapped positions in the canonical form.

**In `gb-player`.** For a chaptered recording, the clock shows the chapter in front
(`c2:00:05:31.400`). Files that are not chaptered show the clock as now. Capture mode's sidebar
shows the position in this form, and *Set start* and *Set end* write it into the settings. The other direction already works: `gb-player GH020073.MP4 --time 331.4` opens chapter 2
at that time.

## The map scale

The map always tracks the current position: centred on the recorded position of the frame, north
up. What is chosen is how much ground it shows, which suits a walk very differently from a flight.
Scale is the **ground distance across the map** (`1.2km`, `800m`), drawn as a scale bar. It does
not depend on the overlay size or the output resolution.

- **Fixed:** `scale = 1.2km`.
- **Automatic:** at each frame, the map shows the recorded route for a **look-ahead time** into
  the future (for example 30 s), plus a margin. Driving into a roundabout, the map widens as the
  exits come into the look-ahead. Stopping while walking the dog, the route ahead shrinks to a
  point and the map closes in. **Closest and widest limits** keep it from zooming into a single
  building or out to a continent. A **response** time sets how quickly it zooms back in. It
  zooms out in time, because the whole curve is computed before rendering
  ([pipeline](pipeline.md#map-scale)).

**Activity presets** give starting values. They are guesses until tuned on real recordings:

| Preset | Look-ahead | Closest | Widest |
|---|---:|---:|---:|
| Walk | 60 s | 150 m | 1 km |
| Hike | 120 s | 300 m | 3 km |
| Bicycle | 45 s | 400 m | 5 km |
| Car | 30 s | 500 m | 15 km |
| Flight | 120 s | 5 km | 200 km |

`--dry-run` reports the automatic scale's range over the export and the times of its widest and
closest points, and `--contact-sheet` shows it.

Where the route ahead has no valid fix, it is left out of the look-ahead. Where the current frame
has no valid fix, the map keeps the last scale and shows the position as unavailable, as the
player does. Nothing is interpolated.

## Map tiles

An export fetches its map tiles itself, as a first stage before rendering. It uses the same
rules and limit, and caches them in the user's tile cache, like `gb-player` when it shows the map.
`--map-no-network` makes it cache-only instead, with missing tiles drawn as placeholders.
`--require-tiles` makes any missing tile an error.

To fetch the tiles once and render offline afterwards, run the export (or a `--draft` of it) once
with the network, then use `--map-no-network --require-tiles`: the tiles stay in the cache, which
has no eviction.

**Not planned: prepared tile directories** (decided 2026-11-01). Earlier versions of this plan had
`--prepare-tiles DIR` (fetch the tiles an export draws into a directory and stop) and
`--map-tiles DIR` (export from that directory only, with a `tiles.ini` recording the provider and
`--allow-missing-tiles`). The user's tile cache already gives most of that: resumable fetching,
fetching only the difference after a settings change, and offline rendering. What a directory
would add, a frozen map that is never revalidated and a set of tiles to move to another machine,
belongs with future work on supporting other map providers and offline map sources.

`--dry-run` reports the tile set: how many tiles, at which zoom levels, how many
are already in the cache, and how many would be fetched.

## Progress and output

Progress goes to stderr: stage (map tiles, then frames), frames written, speed, time remaining.
Ctrl+C cancels. Output is written to `name.partial` and renamed when complete, and a failed or
cancelled export removes the partial file. An existing output is not replaced without
`--overwrite`. The final report lists the file, its size and every warning: tiles that could not
be fetched, no audio, upscaling, GPS gaps in the range.

## Later, if wanted

A GUI could be built on the same settings file at any time, since it would only edit the settings file
and run the same job. It is not planned.
