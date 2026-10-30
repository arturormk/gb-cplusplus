# ADR-0006: Face-specific MAX reconstruction and Qt projection laboratory

Status: Accepted for M3 (2026-10-30)

## Context

The first CPU reconstruction treated the two 4096×1344 video tracks as generic rows of a cube
atlas. Visual evidence, GoPro's published EAC description, and the Apache-2.0 `max2sphere`
reference instead describe two
face-specific equal-angular cube strips: `LEFT / FRONT / RIGHT` and `DOWN / BACK / TOP`. The extra
64 horizontal pixels belong to wrapped duplicate regions on the side faces, not a fourth face or
a uniform seam between tracks.

M3 also requires an interactive laboratory that makes the raw packing, reconstruction, and view
projection directly comparable. It is not a media player: timestamp selection and reproducible
still views are sufficient for this milestone.

## Decision

Use the face-specific MAX mapping in `gb_media`, including per-face rotations, equal-angular cube
coordinates, top-down decoded-image orientation, and linear blending of duplicated side-face
regions. Keep this deterministic CPU implementation as the correctness reference. Provide CPU
equirectangular, rectilinear, equidistant-fisheye, stereographic, and Little Planet projections
with explicit yaw, pitch, roll, and field of view.

Add `gb-project` as a Qt 6 Quick GUI with four simultaneous views: both raw strips, the reconstructed
2:1 panorama, and an interactive virtual camera. Use a Qt scene-graph shader for interaction and
retain the CPU projection as the software-backend fallback and export reference. Require exactly
the verified two-track geometry; do not infer support for other MAX generations or modes.

## Consequences

M3 now has an independently testable six-face reconstruction and a visual comparison tool without
requiring conversion through proprietary software. Qt 6 Core, Gui, QML, Quick, Widgets, Quick
Controls, and ShaderTools become build/runtime dependencies. The tool is timestamp-oriented and
does not provide continuous playback, audio, stabilization, calibration recovery, or telemetry
panels. A black source face remains black and is reported diagnostically rather than synthesized.
