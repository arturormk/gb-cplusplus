# ADR-0007: Native player playback architecture

Status: Accepted for the M4 player baseline (2026-10-30)

## Context

M4 requires a desktop player that opens original MAX `.360` recordings directly, plays and seeks
them smoothly, shows a navigable spherical view, and keeps metadata and telemetry aligned with the
displayed frame. The M3 CPU reconstruction is a correctness reference. At 4096×1344×2 per frame,
it cannot keep up with playback. M3 also showed that software HEVC decoding of both lens streams
is feasible on the development machine: about 88 synchronized frame pairs per second using all
cores. Qt Multimedia's `QMediaPlayer` cannot pair two video streams or apply the MAX face mapping,
and the Qt OpenGL module headers are not part of the supported dependency set.

## Decision

1. Add `gb::media::SynchronizedReader` to `gb_media`. It demuxes the file once, discards
   unselected streams, decodes the selected streams with FFmpeg frame/slice threading, and emits a
   `FrameSet` only when every stream has a frame with exactly the same rational presentation time.
   Unmatched frames are counted, not shown. Frames stay in decoder-owned 8-bit 4:2:0 planes. Other
   pixel formats are converted to that layout. No RGB conversion happens on the CPU.
2. Seeking is accurate. The reader seeks to the preceding keyframe, decodes forward, and returns
   the latest frame set whose PTS is not after the target, or the first set when the target
   precedes it. Targets and positions are integer ticks of the first stream's time base.
3. `select_playback_layout` treats a file as the MAX dual-stream EAC layout only for exactly two
   video streams with equal geometry and time base and `width = 3 × height + even overlap`. Every
   other file is shown flat from its first video stream. No projection is claimed for it.
4. `gb-player` keeps decoding on a worker thread with a bounded queue (four frame sets by
   default). It presents frames against a monotonic wall clock. A frame is presented once its PTS
   is due, and older due frames are counted as late drops. While playing on the GPU path,
   presentation runs once per Qt Quick frame (`afterAnimating`), so every presented frame is
   synchronized for drawing. A 4 ms timer completes seeks and serves the software backend. Seeking
   coalesces requests by generation, so only the latest one is decoded.
5. Display uses an OpenGL `QSGRenderNode` with raw OpenGL 3.3 or ES 3.0 calls. Six `R8` textures
   receive the Y, U and V planes of both streams. One fragment shader converts YUV to RGB, applies
   the face-specific EAC lookup with overlap blending, and projects through the virtual camera.
   That shader is a direct port of the CPU reference, and comments in both require them to stay in
   step. The view offers rectilinear, fisheye, stereographic, Little Planet, equirectangular
   panorama, and both raw tracks as diagnostics.
6. Telemetry is extracted once on a background thread with `gb_telemetry`. The player shows the
   nearest recorded `GPS5` record (`gb::telemetry::nearest_point`, now shared with `gb-time`),
   its tick offset and uncertainty, and any reported gap containing the playback time. It does
   not interpolate positions. The metadata panel parses the `gpmd` sample covering the displayed
   frame and flags unknown keys.
7. There is no audio clock in this baseline. Audio decoding, output, and audio-master sync are
   deferred.

## Consequences

The player depends on the same FFmpeg and Qt 6 packages as M3 and needs no new libraries. It
requires an OpenGL 3.3 or ES 3.0 scene graph to display video. Under the software backend it
still decodes, presents and synchronizes telemetry (as the smoke test does), but shows no
picture. Accuracy of the GPU path against the CPU reference was checked by image comparison, not
by an automated pixel test. Qt 6.2 quirks are handled in code: `QSGRenderNode::matrix()` is only
valid in `prepare()`, and `QQuickWindow::graphicsApi()` reports OpenGL after the software backend
is selected. Qt switches `LC_NUMERIC` to the user's locale, while the format libraries parse
numbers with C functions, so the player restores `LC_NUMERIC=C`. Decoder memory grows with thread
count: about 0.8 GiB peak with automatic threading versus about 0.43 GiB with four threads per
stream, at the same measured smoothness.
