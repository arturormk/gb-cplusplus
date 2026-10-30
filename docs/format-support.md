# Format support

## Implemented

- ISO BMFF identity through a structurally valid top-level `ftyp` box.
- 32-bit, 64-bit extended, and size-to-parent box lengths.
- Checked top-level hierarchy and direct-child recursion for a conservative set of structural
  container boxes.
- UUID box header accounting.
- Lossless FourCC bytes, byte offsets, total sizes, and header sizes in JSON.
- ISO BMFF track IDs, handler types/names, codecs, integer duration/time bases, sample counts,
  video geometry, and basic audio sample-entry fields.
- Bounded `stsd`, `stts`, `stsc`, `stsz`, `stco`, and `co64` parsing; byte-accurate `gpmd` sample
  references.
- Nested GPMF KLV records, primitive big-endian types, raw bytes, `SCAL` normalization,
  `SIUN`/`UNIT`, occurrence/record counts, sample ranges, estimated rates, and unknown keys.
- Friendly per-sample and key-filtered value rendering, including documented `GPS5` component
  labels, encoded values, normalized values, units, and explicit unknown-semantics warnings.
- `ctts` composition offsets and bounded sample mapping for ordinary-sized tracks; count-only
  reporting for exceptionally large fixed-size tracks.
- GPS5 latitude/longitude, WGS 84 altitude, 2D/3D speed, GPSF validity, GPSP DOP, GPSU UTC anchors,
  integer presentation ticks, uncertainty, and missing/invalid gap reporting.
- Schema-versioned JSON/CSV telemetry, validity-filtered GPX/GeoJSON, and media/UTC timing
  diagnostics with nearest-record queries.
- Native FFmpeg video-stream enumeration and software decoding with deterministic nearest-PTS
  selection, PNG/PPM/RGB24 output, and source color metadata.
- Face-specific CPU MAX pseudo-EAC reconstruction with explicit stream selection, retained-overlap
  blending, and deterministic virtual-camera projections.
- Qt 6 projection laboratory comparing both raw tracks, reconstructed equirectangular output, and
  GPU/CPU rectilinear, fisheye, stereographic, and Little Planet views.
- Native playback of the MAX two-stream `.360` layout: exact-PTS frame pairing, accurate seeking,
  and single-pass GPU YUV-to-RGB, EAC reconstruction and virtual-camera projection in `gb-player`.
  Single-stream video plays flat with zoom and pan.
- Content-based JPEG detection with checked marker and entropy termination parsing.
- JPEG dimensions, precision, component count, and directly appended codestream enumeration.
- Structural detection of JFIF, EXIF, XMP, MPF and APP6 payloads.
- Photo metadata: EXIF camera, exposure and GPS fields, XMP GPano crop geometry, and the GoPro APP6
  GPMF settings. Field-of-view classification uses that evidence: 360°, superwide (with its
  horizontal angle), wide, linear, or narrow.
- Photos in `gb-player`: a still view, with spherical views for equirectangular photos, the EXIF
  position on the map, and the capture settings in the panel (ADR-0011).

## Evidence from GoPro MAX (2025) samples

Five owner-provided samples have now established one native `.360` topology, one single-stream
MP4 topology, and three JPEG layouts. See
[`formats/gopro-max-2025-sample-findings.md`](formats/gopro-max-2025-sample-findings.md) for
fingerprints, exact commands, redacted observations, and remaining uncertainties. Both `.360`
HEVC streams decode at synchronized PTS values. The verified six-face projection exposes a
near-black `BACK` source face in stream 5 of the `.360` sample rather than concealing it.

## Not implemented

- Stabilization, optical-flow seam correction, recovered camera calibration, spatial
  (multichannel/ambisonic) audio rendering, and hardware decoding.
- Fragmented-media sample mapping, edit lists, multiple sample descriptions, compressed/complex
  GPMF values, GPS9, sensor-series export, and MPF decoding. EXIF and XMP are interpreted only
  for the fields listed above.
- General payload extraction, HEIF item interpretation, or file repair. Raw `gpmd` sample export is
  explicitly supported by `gb-metadata`.

GPS5 record counts were independently cross-checked with ExifTool. Individual GPS record times
are uniformly assigned within their containing GPMF sample and therefore carry a one-record
interval uncertainty; no capture-time mapping is claimed. General MAX support remains unverified
beyond the fingerprinted samples. Player measurements are recorded in
[`../research/findings/2026-10-30-gb-player-m4-baseline.md`](../research/findings/2026-10-30-gb-player-m4-baseline.md).
