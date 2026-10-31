# Architecture overview

The project follows a tools-first architecture: laboratory utilities establish reproducible
format evidence, shared libraries implement confirmed behavior, and later player/export tools use
those same libraries. See [`BLUEPRINT.md`](BLUEPRINT.md) for the full project direction.

Architecture decisions are stored as individual records under [`adr/`](adr/):

- [ADR-0001: Tools-first container inspection](adr/0001-tools-first-container-inspection.md)
- [ADR-0002: Content-based JPEG inspection](adr/0002-content-based-jpeg-inspection.md)
- [ADR-0003: Bounded native GPMF inspection](adr/0003-bounded-native-gpmf-inspection.md)
- [ADR-0004: Explicit telemetry and clock provenance](adr/0004-explicit-telemetry-clock-provenance.md)
- [ADR-0005: Native FFmpeg frame decoding with a CPU projection reference](adr/0005-native-ffmpeg-frame-decoding.md)
- [ADR-0006: Face-specific MAX reconstruction and Qt projection laboratory](adr/0006-max-reconstruction-and-qt-projection-laboratory.md)
- [ADR-0007: Native player playback architecture](adr/0007-native-player-playback-architecture.md)
- [ADR-0008: Player audio output, audio-master clock and stable-playback gating](adr/0008-player-audio-and-stable-playback-gating.md)
- [ADR-0009: Player map, tile providers and tile cache](adr/0009-player-map-tiles-and-route.md)
- [ADR-0011: Photos in the player](adr/0011-player-photos.md)

## Current boundaries

- `gb-inspect` is a presentation layer over dependency-free format libraries.
- `gb-metadata` inventories and exposes timed GPMF through the same production libraries.
- `gb-telemetry` exports GPS records and `gb-time` diagnoses media/GPS clock relationships.
- `gb-frames` lists and decodes selected video streams and runs a deterministic CPU projection
  reference; it does not provide playback.
- `gb-project` is the Qt visual projection laboratory. It compares both packed source tracks, the
  reconstructed panorama, and an interactive GPU virtual camera against a CPU reference.
- `gb-player` (`apps/gb-player`) is the M4 desktop player. It plays native MAX `.360` and
  single-stream video through the shared libraries, never by invoking a tool. Decoding runs on a
  worker thread, presentation follows a wall clock, and a single OpenGL shader pass goes from
  YUV planes through the MAX face mapping to the view. Telemetry and GPMF panels follow the
  displayed frame. Since M5 a map pane above the panels shows the recorded GPS route and the
  current record from OpenStreetMap tiles, and a click on the route seeks there (ADR-0009).
- `gb_geo` is dependency-free: Web Mercator and tile math, tile provider descriptions and their
  usage policy, the on-disk tile cache and its HTTP freshness rules, and route runs and
  click-to-seek candidates.
- `gb_tiles` (Qt Core and Network) fetches tiles for an interactive view through the `gb_geo`
  cache. It sends conditional requests, limits concurrency, and never prefetches.
- `gb_container` performs bounded, read-only ISO BMFF structural inspection.
- `gb_image` performs bounded, read-only JPEG marker and codestream inspection. It interprets
  EXIF (including GPS), XMP GPano crop geometry and the GoPro APP6 GPMF segment of photos, and
  classifies their field of view from that evidence (ADR-0011).
- `gb_metadata` performs bounded, loss-aware GPMF KLV parsing and normalization.
- `gb_telemetry` assigns recorded GPS fields to checked presentation ticks, classifies fix
  validity, retains timing uncertainty, and exposes GPSU anchors without inventing positions.
- `gb_media` owns FFmpeg demux/decode, the synchronized multi-stream playback reader with
  accurate seeking, playback layout selection, RGB conversion, still-image writing, the shared MAX
  pseudo-EAC reference transform, and deterministic virtual-camera projections.
- Existing ISO BMFF JSON uses `gb.inspect/v1`; JPEG uses `gb.inspect.jpeg/v1`.
- Unknown structures remain visible where possible and are not assigned guessed semantics.

## Unresolved risks

- Fragmented ISO BMFF sample mapping, edit-list application, composition offsets, and multiple
  sample descriptions are not implemented.
- The current parsers require seekable regular files; pipes and remote inputs are out of scope.
- Some ISO BMFF boxes contain internal headers or counted records and intentionally remain opaque
  until dedicated parsers and fixtures are available.
- Only selected EXIF fields and the XMP GPano crop are interpreted; MPF entries are not. Linear and
  narrow photo classification is mapped by analogy and unverified (ADR-0011).
- GPMF complex and compressed values are raw-preserved but not interpreted. GPS5 is mapped to
  GPSU UTC anchors, but sensor capture time, TimeWarp cadence, GPS9, and edit lists remain open.
- The project license remains unresolved. FFmpeg and Qt 6 are now required; enabled codec/build
  license obligations and application distribution still require review.
- The player plays mono or stereo audio only (ADR-0008); the MAX four-channel spatial track is not
  rendered. Audio-to-video offset relies on a one-buffer latency estimate because SDL2 cannot
  report device latency. There is no hardware decoding. Software HEVC decoding of both MAX streams
  uses about 2.3 cores at 25 fps on the development machine.
- The player's GPU face mapping duplicates the CPU reference in GLSL. Their agreement is checked by
  recorded image comparison, not by an automated test, so changes must be made in both.
- The format libraries parse and format numbers with locale-sensitive C/C++ functions. Command-line
  tools run in the C locale, and `gb-player` restores `LC_NUMERIC=C` after Qt starts. Any other
  embedding must do the same until the libraries become locale-independent.
- The shared fisheye, stereographic and Little Planet conventions normalize each output axis
  separately, so non-square outputs are elliptical.
- The fingerprinted `.360` decodes both streams at matching PTS, but its source `BACK` face is
  near-black. Reconstruction preserves that evidence rather than fabricating image content.

- `gb-player` requests map tiles from `tile.openstreetmap.org` by default, which reveals the
  viewed area to that server (never the recording or its track). `--map-no-network` disables it.
  The tile cache has no size limit or eviction. OSM service is best-effort and can be withdrawn.
  The map is raster only and can look soft on HiDPI screens. Routes crossing the antimeridian are
  not drawn across it.
- Map behaviour for invalid fixes, GPS gaps and repeated passes is tested synthetically only; the
  available real samples are valid throughout their GPS coverage.

## ADR convention

Create one Markdown file per decision in `docs/adr/`, named
`NNNN-short-kebab-case-title.md`. Each record should contain status, context, decision, and
consequences. Amend or supersede records explicitly; do not silently rewrite accepted decisions.
