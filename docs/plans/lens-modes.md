# Lens modes: Narrow, Linear, Wide, Max SuperView

Status: Planned for v1.0 (2026-11-01). Partly evidenced: one video and two single-lens photos
have been examined (see below). Most of the mapping from metadata to mode is unverified.

The GoPro MAX (2025) shoots single-lens (HERO-mode) photos and videos through four digital
lenses. GoBro must recognise them from the file's metadata and show them by **the names the
camera uses**:

| GoPro name | Equivalent focal length |
|---|---|
| Narrow | 27 mm |
| Linear | 19 mm |
| Wide | 16 mm |
| Max SuperView | 13 mm |

360° recordings and photos are a separate mode, with no lens setting. The lens is a capture mode,
not a viewing projection (BLUEPRINT §7): Linear is already rectilinear, Wide and Max SuperView
are fisheye-like, and the player's views reproject whatever the lens recorded.

## Evidence so far

**Videos** keep their capture settings in a GPMF block in the container's `moov/udta/GPMF` box,
in a "Global Settings" device. The timed `gpmd` track does not carry them, and the tools do not
read this box yet. Decoded with a throwaway script (2026-11-01):

| Key | `GH050083.MP4` (Max SuperView, per the user) | `GS040047.360` (360°) |
|---|---|---|
| `VFOV` | `X` | absent |
| `ZFOV` | 163.78° | 0.0 |
| `PRJT` | `GPRO` | `EACO` |
| `DZOM` | `N` | `N` |
| `EISE` / `EISA` | `Y` / `HS HLevel` | `N` / `N/A` |
| `MTYP` | 0 | 5 |
| `CMOD` | 12 | 12 |
| `RATE` | `1_10SEC` | `1_10SEC` |

Firmware `H19.03.02.06.00`, `MINF` `GoPro Max` in both. So `VFOV` probably names the lens by a
letter, and `X` is Max SuperView in this sample. Other letters are unknown.

**Photos** carry the same kind of block in their APP6 segment (ADR-0011). The lens shows as the
letter at the end of `PRES` and in `ZFOV`:

| Photo | `PRJT` | `PRES` | `ZFOV` | GoBro shows now |
|---|---|---|---|---|
| `GP__0011.JPG` | `GPRO` | `5.5MP_W` | 148.4° | wide |
| `GP__0026.JPG` | `PANO` | `6.2MP_P` | 360° | superwide (270°) |

`W` is Wide. `L` and `N` are mapped by analogy in `gb_image` and unverified.

**Not trusted alone.** `RATE` is `1_10SEC` in both videos, though neither is a time lapse. Some
settings are probably the camera's last value for another mode, so the mode must come from keys
that agree with each other and with the picture, not from one key.

## Naming clash to resolve

The docs and `gb_image` (`lib/gb-image/src/photo.cpp`) call `GP__0026.JPG` "superwide", and the
sample findings call `GH050083.MP4` "superwide video". The video is Max SuperView. The photo is a
270°×90° crop of the equirectangular canvas (`PANO`, `_P`), which looks more like a panorama mode
than a lens. Find out what the camera calls it, then rename it in code, ADR-0011 and the findings.
"Superwide" should not stay as a GoBro name.

## Work

1. **Samples.** With the MAX (2025), one photo and one short video in each of Narrow, Linear,
   Wide and Max SuperView, plus the 270° photo mode, with a note of what the camera showed for
   each. Add them to `__stuff/` and fingerprint them in
   [`formats/`](../formats/gopro-max-2025-sample-findings.md).
2. **Read `udta/GPMF`.** `gb_metadata` parses the settings block of a video, and `gb-metadata`
   and `gb-inspect` report it, as they already do for photo APP6.
3. **Mode recognition.** One function in a Qt-free library maps the settings (`VFOV`, the `PRES`
   letter, `PRJT`, `ZFOV`) to a mode: Narrow, Linear, Wide, Max SuperView, 360°, the panorama
   mode, or unknown, with the evidence shown next to it, as ADR-0011 does for photos. Unknown
   letters show as "unknown (`VFOV` = `Q`)", not as a guess.
4. **Player.** The panel shows the GoPro name and the focal length for photos and videos.
   Square and the default view follow the mode, not the file name.
5. **`gb-render`.** A text field for the mode, and the mode in `--json`.
6. **Tests.** Synthetic `udta/GPMF` and APP6 blocks for each letter, plus missing, unknown and
   contradictory values.
7. **Docs.** ADR-0011 amended (or a new ADR) for mode names; format support.

## Open questions

1. Which `VFOV` and `PRES` letters does the MAX (2025) write for Narrow, Linear and Wide video
   and Max SuperView photos?
2. What is the camera's name for the 270° photo (`PANO`, `_P`)?
3. Does `MTYP` give the media type (0 video, 5 360° video, 11 photo, 21 360° photo, so far)? If
   so it may also identify TimeWarp and TimeLapse ([plan](timewarp-timelapse.md)).
