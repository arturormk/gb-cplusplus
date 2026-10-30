# BLUEPRINT.md — gb-cplusplus

> **Project:** GoBro (provisional public-facing name)  
> **Repository:** `gb-cplusplus`  
> **Technical prefix:** `gb`, `gb-*` (brand-independent)  
> **Platform:** Linux  
> **Language:** C++20  
> **GUI:** Qt 6 / Qt Quick (QML)  
> **Status:** Initial implementation blueprint

## 0. Mission and central development rule

Build an independent, open-source Linux toolkit for advanced GoPro camera users. The initial hardware target is **GoPro MAX (2025)**. GoBro will ultimately inspect, play, reproject, analyze, and export GoPro photographs and videos—including *original* `.360` recordings—with synchronized metadata and geographic information.

**TOOLS FIRST.** The first deliverables are **permanent, independently useful laboratory utilities** that discover what is actually present in real media files. They are **not disposable demos** and must remain usable as new camera models, codecs, metadata keys, and projection formats emerge.

The engineering direction is:

**tools investigate → documented evidence → shared libraries implement → player and exporter consume.**

Do **not** start by building an elaborate player UI. Do **not** assume a format, metadata field, lens calibration, stitching process, or timing scheme works merely because documentation or a third-party repository suggests it might. Prove capabilities using actual files and reproducible experiments.

### User-facing description

> **GoBro — An independent, open-source toolkit for advanced GoPro™ camera users.**  
> Not affiliated with, endorsed by, or sponsored by GoPro.

The provisional name may change; the technical identity must not.

## 1. Fixed project decisions

1. GitHub repository name: **`gb-cplusplus`**.
2. Build in **C++20** with **Qt 6** for desktop visual tools and the eventual player.
3. Target Linux first; support current Ubuntu distributions initially, but avoid needless distribution coupling.
4. **Native original GoPro MAX `.360` support is mandatory** for the eventual player. Users must not need to convert files manually. Native support can use internal preprocessing/cache, subject to transparent behavior.
5. **GoPro MAX 2025 is the first validation target.** Other cameras and formats are future extensions.
6. First video-export capability: **the original view plus configurable map and telemetry overlays**, not advanced keyframed 360° reframing.
7. All original files are opened **read-only**. Never modify recordings or embed metadata in the originals.
8. Branding is replaceable. Source directories, APIs, binaries, persisted formats, and configuration use `gb` / `gb-*`, not `GoBro`.
9. Standalone tools remain useful and independently testable even once their capabilities are integrated into the main application.
10. Unsupported/unknown metadata must remain inspectable and exportable, not silently discarded.

## 2. Non-goals for the initial phase

- Replicating every feature of GoPro Player or Quik.
- Pixel-identical reproduction of proprietary stitching/stabilization.
- Supporting all historical GoPro formats in the first release.
- Editing original camera files or uploading them to any cloud service.
- Training ML models, automatic cinematic editing, or 3D reconstruction.
- Building a generic nonlinear video editor.
- Comprehensive RAW `.GPR` processing before concrete sample requirements are established.
- Shipping a polished Qt UI before fundamental format and timing evidence exists.

## 3. Repository and naming conventions

Proposed initial layout (evolve only when justified by working code):

```text
gb-cplusplus/
├── BLUEPRINT.md
├── README.md
├── LICENSE
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── apps/
│   ├── gb/                     # Optional unified CLI dispatcher
│   └── gb-player/              # Later Qt desktop player
├── tools/
│   ├── gb-inspect/
│   ├── gb-metadata/
│   ├── gb-telemetry/
│   ├── gb-frames/
│   ├── gb-time/
│   ├── gb-project/
│   ├── gb-calibrate/
│   └── gb-map-render/
├── lib/
│   ├── gb-container/
│   ├── gb-metadata/
│   ├── gb-telemetry/
│   ├── gb-media/
│   ├── gb-timeline/
│   ├── gb-projection/
│   ├── gb-geo/
│   └── gb-render/
├── qml/
├── shaders/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── regression/
│   └── fixtures/
├── research/
│   ├── formats/
│   ├── cameras/
│   │   └── gopro-max-2025/
│   └── findings/
├── docs/
│   ├── architecture.md
│   ├── adr/
│   ├── testing.md
│   ├── format-support.md
│   └── dependencies.md
└── scripts/
```

The initial commit should create **only necessary scaffolding**; empty speculative library trees are not an implementation goal.

### Technical names

- Repository: `gb-cplusplus`.
- CLI family: `gb-inspect`, `gb-metadata`, `gb-telemetry`, `gb-frames`, `gb-time`, `gb-project`, `gb-calibrate`, `gb-map-render`, `gb-render`.
- Main desktop executable: `gb-player`; optional CLI dispatcher: `gb`.
- C++ namespace: `gb`; libraries `libgb-*`; CMake targets `gb_*`.
- Project-specific serialized formats and schema IDs: `gb`, not the provisional brand.
- Use standard XDG configuration/cache/data locations (`~/.config/gb`, `~/.cache/gb`, `~/.local/share/gb` by default); do not hardcode home paths.
- User-facing branding strings must live in centralized replaceable resources/configuration.
- Desktop application ID should be chosen only after a suitable stable reverse-DNS identifier is available; do not assume `org.gb.*` is owned.

### Unified CLI

Standalone commands are primary and must function independently. A later `gb` dispatcher may expose aliases:

```sh
gb-inspect recording.360 --json
gb-metadata recording.360 --json
gb-telemetry recording.360 --format gpx
gb-frames recording.360 --stream 0 --time 00:00:05 --output frame.png
gb-time recording.360 --json
gb-render recording.360 --preset overlay.json --output output.mp4
```

CLI names/options above are **design targets**, not claims of implemented functionality. Use consistent `--help`, exit codes, stdout for structured data, stderr for diagnostics, and `--version`. Avoid breaking output schemas silently.

## 4. Laboratory tools: permanent public deliverables

Every tool must:
- Have a focused purpose and its own README with examples, requirements, and limitations.
- Build as an independent CMake target; only link the smallest necessary shared components.
- Have deterministic text and/or JSON output; JSON must carry schema version.
- Work in a terminal without requiring Qt GUI or a display server unless explicitly visual.
- Return meaningful non-zero statuses on failures; distinguish *unsupported*, *missing*, *corrupt*, and *unexpected*.
- Be robust to malformed, truncated, or malicious media inputs: bounds checks, allocation limits, recursion limits, overflow checks, and fuzz tests where appropriate.
- Include regression tests using synthetic/public fixtures; never commit private GPS tracks by accident.
- Produce research findings and portable fixtures when a new format behavior is discovered.

### 4.1 `gb-inspect` — container/stream inspector (first)

Inspect actual file structure: file signatures; ISO BMFF/MP4 box tree (FourCC, hierarchy, offsets, lengths); streams and codecs; time bases; sample descriptions; vendor-specific boxes; metadata-bearing tracks; unknown structures; relationships among multiple video streams.

Support a concise human overview and detailed `--json`; dump selected box payloads to explicitly requested files; identify raw bytes and offsets without guessing their meaning.

**Exit condition:** on real MAX 2025 `.360`, enumerate the full video/audio/data topology; cross-check with `ffprobe`; preserve unrecognized entries.

### 4.2 `gb-metadata` — loss-aware metadata inspector (first)

Inspect GPMF, EXIF, XMP, QuickTime/container tags, and other identified metadata sources. Preserve nested structure, FourCC, device/stream identity, raw type/scale/units, original value, interpreted value, and provenance. Unknown keys must remain visible.

Use the official GoPro GPMF parser as the reference or dependency after licensing/API review. Note that parsing payloads and locating them in a media container are different responsibilities.

Offer JSON and human/tree views, plus raw payload export. Include field occurrence counts, sample-rate estimates, time ranges, and calibration-related candidates **without promising calibration completeness**.

**Exit condition:** produce a reproducible metadata inventory for the MAX 2025 corpus, independently compared to ExifTool where practical.

### 4.3 `gb-telemetry` — timestamped data extraction (first)

Extract any available GPS position, speed, altitude, UTC timestamps, fix quality, accelerometer, gyroscope, magnetometer, orientation, camera settings, and other time-indexed values.

Formats: schema-versioned JSON, CSV; GPS-based GPX and GeoJSON when suitable. Include units, source, validity, recorded/derived status, and missing-data gaps.

Do not invent GPS positions or extrapolate a missing track; clearly distinguish course-over-ground from camera heading.

**Exit condition:** time-indexed location/time/speed/altitude output on a suitable real GPS-equipped sample; valid handling when no GPS exists.

### 4.4 `gb-time` — clock and synchronization laboratory (first)

Inspect track PTS/DTS, sample duration, GPMF sample timing (`TSMP`, offsets where available), GPS UTC, creation timestamps, and discontinuities. Support timeline comparisons and timing diagnostics.

Represent authoritative times as **integer ticks plus rational time base**, not floating-point seconds; differentiate media presentation time, capture time, and UTC.

TimeWarp/time-lapse may violate one-to-one media/capture-time relationships. Model such uncertainty honestly.

**Exit condition:** report timing anchors, inferred mappings, drift/gaps, and uncertainty; seek to random timestamps and compare telemetry outputs.

### 4.5 `gb-frames` — raw and reconstructed frame extraction (first/second)

Extract decoded frames from a selected source stream at requested timestamps; optionally dump raw pixel buffers with complete pixel-format and color metadata. Later reconstruct a panorama from multiple MAX lens streams.

Allow inspecting intermediate front/rear representations separately. No manual external file conversion should be required by the *finished player*, but an experimental pipeline may temporarily use a reproducible external command.

**Exit condition:** exact stream/frame selection and coherent reconstructed panoramic images at multiple times.

### 4.6 `gb-project` — projection laboratory (second, Qt visual tool)

Experiment with source/output mappings: camera-specific `.360` layout, EAC, equirectangular, rectilinear, fisheye, stereographic/Little Planet, and digital lens modes as supported by evidence. Expose yaw/pitch/roll/FOV and raw/reconstructed/output views.

Prefer GPU shaders for interactive operation, but maintain a tested CPU/reference implementation or deterministic fixture renderer for correctness checks.

**Exit condition:** demonstrate correct orientation/continuity and compare projected landmarks against references; document artifacts and seams.

### 4.7 `gb-calibrate` — calibration laboratory (later)

Explore lens parameters, sensor orientation, distortion, stitching seams, overlap/parallax and any embedded calibration identifiers. Label parameters as **recorded**, **verified inferred**, or **estimated**.

Do not claim to recover unavailable original imagery or proprietary algorithms exactly.

### 4.8 `gb-map-render` — map export experiment (later)

Render a map/route/marker scene to a transparent or compositable offscreen image **without** a running desktop; exercise caching, attribution and permissible tile sources. Avoid one network request per video frame.

Output reproducible snapshots, tile/cache statistics, and failures when offline resources are insufficient.

## 5. Shared architecture: promote evidence, not experiments

The tools should initially share minimal foundational code. When an algorithm is validated, move/generalize it to a tested library and have **both** the laboratory tool and player call the same implementation. Do not copy/paste production algorithms across targets.

Dependencies flow downward:

```text
CLI tools ────────────┐
Qt player ────────────┼──> verified shared libraries
Headless exporter ────┘             │
                                    └──> FFmpeg / GPMF / Qt / MapLibre as appropriate
```

The player may not link a tool executable or rely on shelling out to a lab tool for normal playback. Tool-only experimental adapters can remain outside production libraries. Keep decoder, telemetry, projection, and map-scene APIs GUI-independent wherever possible.

Conceptual APIs (adapt based on experiments):

```cpp
namespace gb {
struct MediaTime { std::int64_t ticks; Rational time_base; };
struct SourceReference { /* original file/track/key/offset */ };
struct TimestampMapping { /* media/capture/UTC anchors and confidence */ };

class MediaInspector;
class MetadataStore;
class TelemetryTimeline;
class VideoDecoder;
class ProjectionEngine;
class MapScene;
class RenderPipeline;
}
```

Data values should retain **raw → normalized → derived** distinctions. Derived results must be reproducible and retain references to their inputs.

### Media model

- Media asset: source file(s), camera/device identity when known, stream layout, capabilities, metadata indexes.
- Video/image track: codec, geometry, projection where known, lens/stream association, time base, color information.
- Metadata track: key hierarchy, device identity, unit, scale, type, sample timestamps.
- Timing model: media time, capture time, GPS UTC, sync anchors and uncertainty.
- Geographic track: valid samples, quality, gaps, interpolated presentation positions.
- Export project/preset: projection and overlay settings independent of GUI.

A sample may be unavailable, malformed, interpolated, or derived; the UI and JSON must not collapse these states into one number.

## 6. Native MAX 2025 `.360`: mandatory, but unverified until tested

Original `.360` compatibility is a product requirement. **Do not assume** every MAX recording contains exactly two video streams, fixed stream indices, an identical EAC layout, or that any proposed FFmpeg patch is shipped in a standard release. Inspect the actual reference files.

A 2024 `gopromax_opencl` FFmpeg patch is useful *research*, not guaranteed upstream functionality. Determine current build/maintenance/licensing status before using or vendoring it. Check metadata for projection/layout and model/firmware differences.

Required diagnostic modes:
1. Container tree and stream list.
2. Individual decoded camera streams.
3. Stitched/reconstructed 360 panorama.
4. Virtual-camera perspective view.

Quality concerns: seams, orientation, color matching, synchronization, depth/parallax, exposure differences, frame rate, audio sync, seeking and reproducibility.

Native support means **opening the original file directly**, not necessarily zero temporary/cache data. The original is always unchanged.

## 7. Photo and shooting-mode support

End-state formats and modes include 360° video/photos; conventional video/photos; SuperView/Superwide, Wide, Linear, Narrow as available; TimeWarp and time-lapse; and supported source projection variants.

**Do not conflate capture mode and geometric projection.** Identify source image geometry and lens mode separately from output viewing projection. Reprojection cannot restore content outside the recorded field of view.

Start with media formats proven by the MAX 2025 corpus. Extend via format capability detection rather than just file extensions. JPEG first; examine MAX 360 photo representation; assess RAW `.GPR` later.

## 8. Desktop application (after tools prove the format)

A native Qt 6 / Qt Quick application, provisional display name GoBro, technical executable `gb-player`.

Core panels:
- Video/photo viewer: pan/tilt/zoom, pause, seek, full screen, raw/reconstructed/projected diagnostic views.
- Metadata inspector: complete structured inventory and source/provenance.
- Live telemetry: date/time (clearly label UTC/local/capture), latitude, longitude, altitude, speed, fix/quality; orientation and sensors where available.
- Map: OSM-compatible map, recorded route, current marker, follow mode, click track to seek.
- Timeline: playback time, metadata sample positions, gaps and seek controls.
- Projection controls: current source/output mode; yaw/pitch/roll/FOV as appropriate.
- Export settings: output format, position/size/opacity of map, telemetry fields and style.

A missing datum should display as unavailable rather than as zero. GPS route clicking may be ambiguous if coordinates repeat: resolve by nearest time or present candidate times. Seeking and scrubbing must keep telemetry and map synchronized.

Use a shared projection definition between player and export, but don't require the interactive QML map widget to be the export renderer.

### Playback stack experiments

- FFmpeg libraries for demuxing and decoding are the primary candidate.
- Qt Quick custom scene graph/OpenGL path is the initial viewer candidate, pending performance and compatibility tests.
- Examine hardware acceleration (VA-API, other driver paths) **after** correct software decoding.
- Explicit frame queues and seek/flush handling; bounded memory; measured dropped frames/seek latency.
- Do not assume a standard `QMediaPlayer` pipeline can solve dual-stream `.360` reconstruction.

## 9. Maps, attribution and headless export

Use OpenStreetMap-compatible data/maps, with **MapLibre Native Qt** as the first interactive-map candidate. Pin a tested Qt 6-compatible version and validate its QML rendering backend.

A reusable map scene describes map style, center, bearing, zoom, geographic route, marker, and attribution. UI and offscreen output should interpret this same scene.

**Never bulk-fetch official OSM tiles for rendering a video.** Support approved tile sources and/or locally cached/offline map assets; cache backgrounds and render the moving marker independently. Display appropriate attribution, including in exported content.

First export: flat rendered video using the original view where applicable, with map and telemetry overlays; configurable opacity, size, corner/coordinates, unit preferences and time zone. Preserve audio and verify synchronization. Distinguish this from permanently burning a viewport HUD into a navigable 360° file; true interactive-360 overlay semantics are later work.

Video export settings should be serializable in versioned JSON (or another explicitly documented format). Example *conceptual* preset:

```json
{
  "schema_version": 1,
  "output": {"width": 1920, "height": 1080, "codec": "h264"},
  "map": {"enabled": true, "anchor": "bottom-right", "opacity": 0.8},
  "telemetry": ["gps_utc", "latitude", "longitude", "altitude", "speed"]
}
```

Headless `gb-render` must not require a display server. Codec/licensing combinations should be documented. Export should report missing offline tile coverage instead of silently yielding a misleading map.

## 10. Research and test corpus

Prioritize a **small, controlled MAX 2025 corpus**:

1. Short original `.360` recording with visible landmarks and GPS if available.
2. Conventional video with metadata.
3. 360° still photo.
4. Conventional still photo.
5. TimeWarp/time-lapse sample.
6. Optional stationary clip and moving clip to test time, speed, orientation and seams.
7. Malformed/truncated synthetic inputs for robustness.

Track camera model, firmware, capture mode, resolution, frame rate, lens settings, duration, expected tracks, metadata keys, and whether GPS is present. Store sensitive original clips **outside the public repository** unless sharing is explicitly authorized. Use sanitized or synthetic public fixtures; redact exact routes, serial numbers and other personally identifying metadata.

Record discoveries under:

```text
research/cameras/gopro-max-2025/
  README.md
  container.md
  metadata.md
  timing.md
  projection.md
  calibration.md
  limitations.md
  evidence/
```

For each finding, cite the tool version/commit, test file fingerprint (hash where safe), command, observations, hypothesis vs confirmed conclusion, and reproduction steps. Publish a sanitized structural **format fingerprint** when possible.

## 11. Milestones and release gates

### M0 — Build/repository baseline

**Status: Complete (2026-10-29).** The fresh-checkout gate is covered by the pinned Ubuntu 22.04
CI build and was independently reverified with an out-of-tree GCC 11.4 build, warnings as errors,
and all tests passing without private camera samples. M1 has since completed.

- Working CMake/Ninja C++20 Linux build, test harness (`ctest`), formatter/linter configuration, README and contribution guidance.
- Dependency discovery and pinned/versioned requirements.
- Minimal `gb-inspect --help`, CI build, and at least one parser test.
- **Gate:** fresh checkout builds and tests without private camera samples.

### M1 — Container + metadata laboratory (highest priority)

**Status: Complete (2026-10-29).** `gb-inspect` independently reproduces the fingerprinted MAX
2025 video track topologies checked against FFprobe. `gb-metadata` maps every `gpmd` payload,
preserves raw/unknown GPMF, reports normalized primitives and timing provenance, and matches
ExifTool's privacy-safe embedded GPS and sensor occurrence counts. Synthetic fixtures cover
unknown, scaled, truncated, and sample-table behavior without private media. M2 has since
completed.

- Independently usable `gb-inspect` and `gb-metadata`.
- Real .360 structure documented and checked against `ffprobe`.
- GPMF extraction, raw/normalized metadata, unknown fields preserved.
- Independent comparison with ExifTool.
- **Gate:** reproducible reports from original MAX 2025 files, with clear unsupported behavior.

### M2 — Telemetry and timing laboratory

**Status: Complete (2026-10-29).** `gb-telemetry` exports recorded `GPS5` position, altitude,
speeds, fix and DOP as JSON, CSV, GPX, and GeoJSON, with invalid/missing handling and explicit
gaps. `gb-time` reports integer decode/presentation timing, UTC anchors, discontinuities, drift,
and nearest-record queries. Both fingerprinted videos reproduce the M1 GPS counts and have been
checked at selected media ticks without publishing private routes. Synthetic tests cover absent
GPS, invalid fixes, non-integral record spacing, composition offsets, and malformed input. M3 has
since completed.

- `gb-telemetry` and `gb-time` with GPS extraction, units, validity, and media/GPS time mapping.
- GPX/GeoJSON/CSV/JSON, missing GPS cases, gaps, corruption and sample jitter tests.
- **Gate:** verify timed position/speed/altitude on real samples at selected timestamps; documented uncertainty.

### M3 — Native video reconstruction laboratory

**Status: Complete (2026-10-30).** `gb-frames` directly enumerates and decodes either fingerprinted
HEVC stream with exact integer PTS/color reporting. The shared CPU reference implements the
face-specific MAX layout (`LEFT / FRONT / RIGHT` and `DOWN / BACK / TOP`), EAC transform, face
rotations, retained-overlap blending, and deterministic virtual-camera projections. `gb-project`
provides the required Qt comparison of both raw tracks, the reconstructed panorama, and an
interactive GPU view with a CPU reference/export path. Synthetic six-face fixtures verify the
mapping and multiple projection modes; real frames at multiple timestamps verify decoding,
orientation, and source diagnostics. The fingerprinted recording's `BACK` source face is itself
near-black and remains visible as such; reconstruction does not manufacture missing pixels. M4 has
since met its baseline gate.

- `gb-frames` extracts both original video streams; reconstructed panoramic stills from multiple timestamps.
- `gb-project` visual comparison of raw, reconstructed and virtual-camera views.
- Document projection transforms, stitching seams, limitations and whether `.360` photos require distinct handling.
- **Gate:** correct navigable panorama directly from original MAX 2025 `.360` with no manual user conversion.

### M4 — Native Qt player baseline

**Status: Baseline gate met (2026-10-30).** `gb-player` opens the fingerprinted `.360` directly,
decodes both HEVC lens streams in exact PTS lockstep, and reconstructs and projects them in one
GPU shader pass that ports the M3 CPU reference. It plays the single-stream MP4 flat. Telemetry
and per-sample GPMF metadata follow the displayed frame without interpolation. Over 60 s on the
development machine, both samples played at 25 fps with 0 dropped frames, 0 underruns and 20/20
exact random seeks. Measured `.360` seek latency was 354 ms median and 520 ms maximum, at about 2.3
cores of CPU, 2.4 ms GPU time per frame, and 0.43–0.79 GiB peak RSS depending on decoder
threads. See
[`research/findings/2026-10-30-gb-player-m4-baseline.md`](../research/findings/2026-10-30-gb-player-m4-baseline.md).
Audio was added to complete M4 (ADR-0008). The stereo AAC track plays with an audio-master clock
and sample-accurate alignment to the video clock. It is heard only during stable playback: never
while scrubbing, while a seek is decoding, within 250 ms after a seek, or while paused. On both
samples, 20 s runs played with 0 dropped frames and 0 audio underruns, and the benchmark recorded
no audible frames during scrubs, seek bursts or pauses. Hardware decoding, spatial audio and
photos remain outside M4.

- Open file, play/pause/seek, spherical viewing, metadata panel.
- Synchronize telemetry and media timeline; basic performance diagnostics.
- **Gate:** representative clip opens directly and supports stable playback and seeking on development hardware. Record fps, dropped frames, CPU/GPU use, memory and seek latency.

### M5 — Geography and maps

**Status: Gate met (2026-10-30).** `gb-player` shows a live OpenStreetMap raster map of the
recorded route. The route is drawn per run of valid fixes and split into travelled and remaining
parts. It shows the current record with its direction of travel, follow mode, zoom and pan, and
mandatory attribution. Clicking the route seeks to it, and repeated passes are offered as
separate times. Missing GPS, invalid fixes and reported gaps are shown as such; nothing is
interpolated. Tile providers are data (`gb::geo::TileProvider`). Tiles are cached under XDG cache
by HTTP caching rules, revalidated conditionally, fetched two at a time, and never prefetched
(ADR-0009). Measured on both samples, the painted marker matched the record nearest to the
displayed PTS in every sample during playback, after the pause, and after 20/20 random seeks per
run, with no frame loss. See
[`research/findings/2026-10-30-gb-player-m5-map.md`](../research/findings/2026-10-30-gb-player-m5-map.md).
Invalid-fix, gap and repeated-pass behaviour is tested synthetically only. MapLibre and Qt
Location are unavailable for Qt 6.2, so the map uses its own renderer.

- Live OSM-compatible map, current position, route, click-to-seek.
- Missing GPS and ambiguous repeated coordinates handled gracefully.
- **Gate:** map/telemetry/video remain aligned during play, pause and random seeks.

### M6 — Headless and GUI overlay export (initial useful release)

- `gb-map-render` proves offline/cached export; `gb-render` implements map + telemetry overlays.
- GUI export presets; MP4 export with audio synchronization and attribution.
- **Gate:** representative exported frames match telemetry at corresponding player timestamps; no uncontrolled tile requests; reproducible CLI export.

### Later

Advanced calibration/stabilization, keyframe-based 360 reframing, new GoPro models, additional codecs and photo formats, multi-chapter file grouping, more HUD styles, performance optimizations, packaging.

Each milestone must leave its tools useful. A milestone is **not complete** if it only works through the player GUI or undocumented one-off scripts.

## 12. Testing, correctness, security, performance

- Unit tests for ISO BMFF hierarchy, endianness, scaling, bounds checking, JSON schemas, rational-time arithmetic, interpolation policies, and projection math.
- Regression fixtures for known metadata keys, unknown keys, corrupt or truncated payloads, absent GPS, non-monotonic timestamps, duplicate coordinates, and GPS fix changes.
- Use fuzzing/sanitizers (ASan/UBSan) for binary parsers in development/CI when feasible.
- Integration tests should assert expected structure and values against external reference tools and curated samples.
- Golden images for projection/regression where legally distributable; document tolerance and transformations.
- Benchmark real clips for startup, inspect speed, decode fps, CPU/GPU load, memory footprint, seek latency and export throughput.
- Never load entire video streams or arbitrarily large box payloads into memory merely to inspect structure.
- Avoid network access by default for analysis tools; map provider configuration and telemetry must be explicit. Never transmit private source recordings or GPS to unrelated services.
- Keep generated outputs and caches outside version control.

## 13. Dependencies and licensing

Candidates, not assumed implementation facts:

- **Qt 6.5+**, Qt Quick/QML, Qt Core, suitable multimedia/window modules.
- **FFmpeg libraries** (libavformat, libavcodec, libavutil, libswscale as needed).
- **GoPro GPMF parser** (MIT/Apache-2.0 dual licensed) after API review.
- **ExifTool** for diagnostics/reference; not a required runtime subprocess unless explicitly justified.
- **MapLibre Native Qt** for interactive mapping, version-pinned; evaluate headless renderer separately.
- OpenGL initially for custom 360 projection, abstracted enough to adapt later.

Review license obligations for every dependency and enabled FFmpeg codec/build option. Proposed project license is **GPL-3.0-or-later subject to final compatibility and distribution review**; do not claim a final license until selected. Preserve third-party notices.

GoBro is a **provisional** brand; `gb` identifiers are designed to survive rebranding. Non-commercial status or a disclaimer does not by itself guarantee trademark permission. Do not use GoPro logos or imply endorsement.

## 14. Codex operating instructions

When implementing work under this blueprint:

1. **Start with M0/M1 and the laboratory utilities. Do not build the polished player first.**
2. Before choosing an external library or obscure API, verify its current behavior, version, license, and build requirements; document links and commits.
3. Before making a format claim, request or inspect real sample evidence; never silently assume properties of MAX 2025 `.360`.
4. Keep commits small and useful: a tool, tests, docs, then measured results.
5. For each new binary format behavior, update `research/` with reproduction commands and observations.
6. Prefer simple readable code and library boundaries proven by actual shared use; avoid speculative abstraction.
7. Ensure all tools build and work independently through CMake, with machine-readable stable output.
8. No automatic uploading, destructive file writes, or silent source modification.
9. Keep temporary processes, files and caches explicit and removable.
10. Record architecture decisions as individual files under `docs/adr/`, maintain unresolved
    cross-cutting risks in `docs/architecture.md`, and record format evidence in
    `research/findings/`.
11. When a capability is implemented in a library, refactor the corresponding tool to use that same production implementation.
12. Report tests run, results, relevant measurements, dependencies added, limitations, and next recommended experiment in every substantial change.
13. If a prerequisite sample is unavailable, build the general inspection path and fixtures first; mark conclusions about actual MAX media as **unverified**.
14. Avoid working on multiple unrelated milestones in one change.
15. Preserve CLI output compatibility; version schemas and note breaking changes.

### First requested Codex task

Implement **M0 and the first thin slice of M1**:

- Create a buildable C++20/CMake/Ninja repository named `gb-cplusplus`, README, test baseline and `gb-inspect` target.
- `gb-inspect` takes a file and emits basic validated identity, size and top-level ISO BMFF box hierarchy with checked offsets/lengths; implement `--json`, `--help` and meaningful error handling.
- Write synthetic fixtures and tests for valid, truncated and malicious box-size cases.
- Document exact Ubuntu/Qt/FFmpeg dependencies needed now (do not require unused dependencies in M0).
- Document the next steps to identify MAX 2025 video/data streams.
- **Do not implement a Qt player, complex stitching, a map, or export in this first task.**

## 15. Reference research links

These are research inputs, not promises of compatibility:

- [GoPro GPMF parser and format documentation](https://github.com/gopro/gpmf-parser)
- [Proposed FFmpeg GoPro MAX OpenCL filter (2024)](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-September/333649.html)
- [FFmpeg filters and v360 documentation](https://ffmpeg.org/ffmpeg-filters.html)
- [MapLibre Native Qt](https://github.com/maplibre/maplibre-native-qt)
- [GoPro trademark guidance](https://gopro.com/en/gi/legal/trademarks)
- [OpenStreetMap tile usage policy](https://operations.osmfoundation.org/policies/tiles/)

**Long-term guiding principle:** GoBro should help people understand what their cameras *really recorded*, not merely view the pixels that a conventional media player happens to decode.
