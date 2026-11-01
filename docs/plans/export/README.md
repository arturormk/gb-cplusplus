# Export plan

Status: In progress (2026-10-31). Capture mode in `gb-player`, with the shared renderer it needs,
is implemented (ADR-0012). `gb-render` exports video with text and map overlays, over the picture
or on a panel, across chapters (ADR-0013, [`tools/gb-render`](../../../tools/gb-render/README.md));
with a fixed or automatic map scale; stills, activity presets and graphs are not done yet.
Prepared tile directories are not planned.

This plan covers exporting stills and videos from GoPro recordings with **`gb-render`**, a
command-line tool, which is also the headless renderer that BLUEPRINT M6 requires, and with a
**Capture mode in `gb-player`** that frames and captures stills and shows that framing as
`gb-render` settings. It is split into five documents:

- [`capture.md`](capture.md): Capture mode in `gb-player`: aspect ratio, capture rectangle,
  stills, and the settings sidebar.
- [`cli.md`](cli.md): the workflow, the command line, chapter positions, the map scale.
- [`settings.md`](settings.md): the INI settings file (`gb.export/1`) that describes the view,
  frame, overlays and output.
- [`pipeline.md`](pipeline.md): the components behind export (shared renderer, overlay painters,
  sequence reader, encoder, still writer) and how a frame is produced.
- [`phases.md`](phases.md): the order of work, the spikes, testing, the ADRs to write, and open
  questions.

## What export must do

1. **Range.** For video, an interval, which may cross the boundaries between contiguous chapters
   of one recording, given as chapter positions that can be read off `gb-player`. For a photo, or
   for one frame of a video, a still.
2. **Framing.** A projection and a capture rectangle chosen on a preview, at a common aspect ratio (16:9, 4:3, 1:1, 9:16)
   or a free one, and an output resolution.
3. **Overlays.** Map, speed, height and telemetry text, drawn *over* the picture or *beside* it on
   a black panel. The map tracks the current position at a fixed scale, or at an automatic one
   that keeps the route ahead in view, within limits.
4. **Outputs.** Stills (JPEG, PNG) and videos (MP4 or MKV, H.264 or HEVC). Audio in videos is
   optional. Telemetry is not embedded in exported videos. Stills can optionally carry GPS EXIF.
5. **Checking before exporting.** Single stills, a contact sheet over the range, a fast draft of a
   short segment, and a dry run.

## Decisions this plan proposes

These are summarised here and argued in the linked documents. Each one marked ADR gets its own
record under `docs/adr/` when the work is done.

| # | Decision | Where |
|---|---|---|
| 1 | Video export is a **command-line tool, `gb-render`**. Every export is described by settings files and options, so it can be repeated exactly. (ADR) | [cli](cli.md) |
| 2 | Framing is done in a **Capture mode in `gb-player`**: aspect ratio, a movable and resizable capture rectangle over the view, stills captured at a chosen resolution, and the framing shown as settings to copy. Play mode is unchanged. (ADR) | [capture](capture.md) |
| 3 | Settings are an **INI file**, `gb.export/1`: one section per part and per overlay, with comments, no new dependency, strictly validated. Several files and options combine, later ones winning. | [settings](settings.md) |
| 4 | Overlay geometry is in **fractions of the frame height**, so stills, drafts and the full export have the same layout at any resolution. | [settings](settings.md#units-and-sizes) |
| 5 | Positions in a chaptered recording are written **`c2:00:05:31.400`**: chapter, then the time within it in the player's clock format. `gb-player` shows that form on its clock and in Capture mode. | [cli](cli.md#positions-in-a-chaptered-recording) |
| 6 | Frames are rendered **offscreen on the GPU with the player's shader**, moved into a renderer that `gb-player` and `gb-render` share. The CPU projection stays as the reference and becomes an automated test oracle. (ADR) | [pipeline](pipeline.md#video-renderer) |
| 7 | Overlays are drawn with **QPainter by painters shared** with the player's map and graph. | [pipeline](pipeline.md#overlay-painters) |
| 8 | The map **always tracks the current position**, north up. Its scale is a ground distance, **fixed or automatic**: automatic keeps a configurable look-ahead of route in view, within closest and widest limits, from a curve computed in advance, with activity presets. (ADR) | [cli](cli.md#the-map-scale), [pipeline](pipeline.md#map-scale) |
| 9 | Export **fetches and caches the map tiles it needs**: exactly the tiles it draws, before rendering, through the player's rate-limited fetcher, up to a tile limit. Prepared tile directories (`--prepare-tiles`, `--map-tiles`) are not planned (2026-11-01); a run with the network fills the cache, and later runs can be offline. `--map-no-network` makes it cache-only, with missing tiles marked. Attribution is always drawn. (ADR, amends ADR-0009 and BLUEPRINT §9) | [pipeline](pipeline.md#map-tiles-in-export) |
| 10 | **One output frame per source frame**, at the source's exact frame rate and with its PTS rebased to the start of the range. No frame-rate conversion in the first version. | [pipeline](pipeline.md#timing) |
| 11 | A range over several chapters is read through a **sequence reader** that rebases each chapter's video, audio and telemetry onto one recording timeline. Chapters were measured to be exactly contiguous on a three-chapter MP4 recording. (ADR) | [pipeline](pipeline.md#chapter-sequences) |
| 12 | GPS EXIF in stills is **off by default** and taken only from a recorded fix, never interpolated. Exported videos carry **no location metadata** of any kind. (ADR) | [pipeline](pipeline.md#still-writer) |

## Out of scope for the first version

- A video export GUI. Capture mode frames and captures stills; video is exported with `gb-render`.
- Overlays in Capture mode. They are rendered by `gb-render`.
- Framing that changes over time (keyframed reframing). BLUEPRINT lists this under "Later". The
  settings format reserves a section for it.
- Exporting a navigable 360° file with spherical metadata. The equirectangular Panorama view can
  be exported as a flat 2:1 picture, and that is all.
- Frame-rate conversion, slow motion, TimeWarp retiming.
- Hardware encoders (VA-API, NVENC). These come later, behind the same encoder interface.
- Rendering the four-channel ambisonic MAX audio. Export uses the same mono/stereo audio
  selection as playback (ADR-0008).
