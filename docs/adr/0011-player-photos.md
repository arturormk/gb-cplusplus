# ADR-0011: Photos in the player

Status: Accepted (2026-10-30)

## Context

The MAX writes photos as JPEG files: `GP__nnnn.JPG` for single-lens (HERO-mode) photos, including
the superwide crop, and `GS__nnnn.JPG` for 360° photos. The player opened only video. A photo has
no timeline, so it has no route, height or speed graph, or audio. What a photo does carry is a
capture position, exposure settings, and evidence of its field of view. ADR-0002 deliberately left
the JPEG metadata payloads uninterpreted.

The three sample photos (see the MAX sample findings) record this evidence:

| Photo | XMP GPano crop of full canvas | APP6 `PRJT` | APP6 `PRES` | APP6 `ZFOV` |
|---|---|---|---|---|
| normal | absent | `GPRO` | `5.5MP_W` | 148.4° |
| superwide | 4320×1440 at (720, 720) of 5760×2880 | `PANO` | `6.2MP_P` | 360° |
| 360 | 5760×2880 of 5760×2880 | `ERP0` | `16.6MP_S` | 360° |

Only the 360 photo has EXIF GPS. The superwide and 360 photos record no EXIF exposure time or ISO.

## Decision

- The player recognises photos by the JPEG signature, not the extension, as ADR-0002 requires.
- `gb_image` interprets the payloads it previously only identified. This is bounded and
  loss-aware, and nothing is thrown for damaged optional metadata:
  - EXIF IFD0, the EXIF IFD and the GPS IFD, in either byte order. Offsets are checked, entry
    counts bounded, and a damaged entry is skipped.
  - XMP GPano crop geometry. A crop that does not fit its canvas is rejected.
  - The GoPro APP6 segment, which is `GoPro\0` followed by GPMF. It is parsed with `gb_metadata`;
    `gb_image` now depends on `gb_metadata`, and both remain dependency-free.
- The field of view is classified from the strongest evidence. An equirectangular GPano crop
  gives 360° when it covers the whole canvas, and otherwise superwide with its horizontal angle
  (270° here). Without GPano, `PRJT` (`ERP0`, `PANO`) is used, then the lens letter at the end of
  `PRES`. `W` is the only letter observed. `L` (linear) and `N` (narrow) are mapped by analogy and
  are unverified. The evidence is shown next to the result. The user-facing mode name "normal" is
  the camera's Wide lens.
- The picture is decoded once through FFmpeg (`gb::media::decode_still`), without seeking.
  FFmpeg's still-image demuxer yields no frame after a seek. The engine shows the frame set as a
  still, with no decoder thread, clock, audio or seeking. Deprecated full-range YUVJ input is
  converted with an explicit, range-preserving swscale setup.
- An equirectangular photo is a third source layout. The shader samples the GPano crop inside the
  full 360×180° canvas, so the rectilinear, fisheye, stereographic, Little Planet and panorama
  views apply to it. Directions outside a superwide crop show the background. Other photos are
  shown flat.
- The UI drops the transport row, timeline, height/speed graph and track export for photos. The
  map shows the EXIF position as a single point, with no start or end markers and no course or
  orientation.

## Consequences

- MPF entries other than the first codestream (the dependent preview) are not used.
- The camera-orientation fields in APP6 (`GRAV`, `IORI`, `CORI`) are listed in the metadata panel
  but not applied to the view.
- Linear and narrow classification needs real photos before it can be relied on.
- A photo is decoded on the UI thread. For the 16.6 MP sample, decoding and inspection took about
  0.2 s in a Debug build on the development machine.
