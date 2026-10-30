# Changelog

Notable changes per release series. Odd minors are development series whose patch is the Git
commit count; even minors are manually versioned releases (see
[ADR-0010](docs/adr/0010-versioning-and-releases.md)).

## 0.1 — first public development series

First public release: a minimum usable product for GoPro MAX 2025 recordings on Linux.

- `gb-player`: plays original MAX `.360` files directly with GPU spherical viewing, accurate
  seeking, synchronized telemetry and metadata panels, and audio that plays only during stable
  playback. It shows a live OpenStreetMap route map with click-to-seek and has a benchmark mode.
- `gb-inspect`: ISO BMFF container and content-based JPEG inspection.
- `gb-metadata`: GPMF metadata inventory, sample decoding, and raw payload export.
- `gb-telemetry`: timestamped GPS export to JSON, CSV, GPX, and GeoJSON.
- `gb-time`: diagnostics for the relationship between the media and GPS clocks.
- `gb-frames`: native frame extraction and deterministic MAX EAC reconstruction.
- `gb-project`: Qt visual comparison and an interactive virtual camera.
- Licensed under GPL-3.0-or-later. Versioning follows the odd/even scheme of ADR-0010.
