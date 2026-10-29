# Architecture overview

The project follows a tools-first architecture: laboratory utilities establish reproducible
format evidence, shared libraries implement confirmed behavior, and later player/export tools use
those same libraries. See [`BLUEPRINT.md`](BLUEPRINT.md) for the full project direction.

Architecture decisions are stored as individual records under [`adr/`](adr/):

- [ADR-0001: Tools-first container inspection](adr/0001-tools-first-container-inspection.md)
- [ADR-0002: Content-based JPEG inspection](adr/0002-content-based-jpeg-inspection.md)
- [ADR-0003: Bounded native GPMF inspection](adr/0003-bounded-native-gpmf-inspection.md)
- [ADR-0004: Explicit telemetry and clock provenance](adr/0004-explicit-telemetry-clock-provenance.md)

## Current boundaries

- `gb-inspect` is a presentation layer over dependency-free format libraries.
- `gb-metadata` inventories and exposes timed GPMF through the same production libraries.
- `gb-telemetry` exports GPS records and `gb-time` diagnoses media/GPS clock relationships.
- `gb_container` performs bounded, read-only ISO BMFF structural inspection.
- `gb_image` performs bounded, read-only JPEG marker and codestream inspection.
- `gb_metadata` performs bounded, loss-aware GPMF KLV parsing and normalization.
- `gb_telemetry` assigns recorded GPS fields to checked presentation ticks, classifies fix
  validity, retains timing uncertainty, and exposes GPSU anchors without inventing positions.
- Existing ISO BMFF JSON uses `gb.inspect/v1`; JPEG uses `gb.inspect.jpeg/v1`.
- Unknown structures remain visible where possible and are not assigned guessed semantics.

## Unresolved risks

- Fragmented ISO BMFF sample mapping, edit-list application, composition offsets, and multiple
  sample descriptions are not implemented.
- The current parsers require seekable regular files; pipes and remote inputs are out of scope.
- Some ISO BMFF boxes contain internal headers or counted records and intentionally remain opaque
  until dedicated parsers and fixtures are available.
- JPEG metadata payloads are identified structurally but EXIF, XMP, MPF, and APP6 fields are not
  yet interpreted by the project libraries.
- GPMF complex and compressed values are raw-preserved but not interpreted. GPS5 is mapped to
  GPSU UTC anchors, but sensor capture time, TimeWarp cadence, GPS9, and edit lists remain open.
- The project license and later Qt/FFmpeg dependency choices remain unresolved.

## ADR convention

Create one Markdown file per decision in `docs/adr/`, named
`NNNN-short-kebab-case-title.md`. Each record should contain status, context, decision, and
consequences. Amend or supersede records explicitly; do not silently rewrite accepted decisions.
