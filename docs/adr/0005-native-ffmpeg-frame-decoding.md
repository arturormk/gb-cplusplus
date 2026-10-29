# ADR-0005: Native FFmpeg frame decoding with a CPU projection reference

Status: Accepted for the `gb-frames` M3 slice (2026-10-30)

## Context

The fingerprinted `.360` contains two synchronized HEVC streams. Requiring users to pre-convert
them would violate the native-input goal. The proposed FFmpeg `gopromax_opencl` filter remains a
mailing-list patch rather than a capability available in the Ubuntu 22.04 FFmpeg build, and a GPU
filter alone would not provide a small deterministic correctness reference.

## Decision

Add `gb_media`, linked to the public FFmpeg demux/decode and scaling libraries. Decode a selected
stream directly, seek backward, flush, and choose the nearest decoded presentation frame with an
explicit earlier-frame tie policy. Convert once to full-range packed RGB24 while preserving the
source frame's color description. Keep image encoding and the MAX pseudo-EAC-to-equirectangular
CPU transform in the shared library so later tools do not shell out to `gb-frames`.

The projection is capability- and geometry-driven: callers explicitly select two streams and an
overlap. It must not infer lens identity from fixed stream indices or claim stabilization and
calibration that are not implemented. Visual defects in evidence files are reported, not filled.

## Consequences

M3 introduces FFmpeg development/runtime dependencies and their distribution/license obligations.
PNG, PPM, and RGB24 extraction now works without manual conversion. The CPU transform is suitable
for tests and still-image experiments, not playback. GPU projection, calibration, mode detection,
edit lists, and the unexplained missing face observed in the reference clip remain separate work.
