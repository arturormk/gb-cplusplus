# Testing

Configure, build, and run all tests with:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`gb_container_tests` creates its binary fixtures in a uniquely named temporary directory and
removes them at exit. Fixtures cover a valid nested ISO BMFF hierarchy, a truncated box, a box
whose 32-bit size exceeds the file, an extended-size box smaller than its header, and a size-zero
box extending to the end of its parent. No camera media or location data is stored in the tree.

`gb_image_tests` generates JPEGs with metadata markers, byte-stuffed entropy data, restart markers,
and an appended preview codestream. It covers valid geometry/signatures, truncated and malicious
segment sizes, and a missing end-of-image marker.

`gb_metadata_tests` covers nested KLV parsing, big-endian primitive decoding, component scaling and
units, unknown-key raw preservation, and truncated-record rejection. The CLI fixture builds a
complete synthetic `gpmd` track with `stsd`/`stts`/`stsc`/`stsz`/`stco`; integration tests verify
both inventory and detailed-sample JSON schemas without private media.
Friendly-text integration fixtures also verify labeled `GPS5` components, explicit unknown-key
marking, key filtering, and opt-in whole-track value output.

`gb_telemetry_tests` and CLI fixtures cover GPS5 units, GPSU conversion, GPSF validity, GPSP DOP,
integer media ticks, non-integral record spacing, composition offsets, invalid-fix gaps, and a
metadata track with no GPS. Integration tests exercise JSON, CSV, GPX, GeoJSON, timing diagnostics,
and nearest-record lookup. Invalid points are retained in loss-aware JSON/CSV and omitted from GPS
route formats by default.

`gb_media_tests` checks bounded pseudo-EAC reconstruction, six distinct synthetic face colors,
source-strip participation, face routing, virtual-camera directions and projection modes, color
preservation, and geometry rejection. FFmpeg generates a two-video MPEG-4 fixture during CTest;
integration tests list both streams, select stream 1 at a timestamp, encode PNG, and reconstruct a
PPM without requiring private media. An offscreen `gb-project` smoke test loads the complete QML
scene and compiled shader resources without requiring a display server.

`gb_playback_tests` covers playback layout selection (verified MAX geometry, odd overlap,
mismatched streams, no video), floor-rounded tick rescaling and overflow rejection. It also runs
the synchronized reader against an FFmpeg-generated 20-frame, two-stream MPEG-4 fixture with a
5-frame GOP. The reader must pair every frame, keep the PTS sequence exact, preserve stream order
and limited-range planes, seek accurately inside a GOP, hold the last frame past the end, and
reject invalid selections. The same executable checks audio stream selection. Stereo is chosen,
and MAX-style four-channel audio and missing audio are rejected. `AudioReader` must produce
contiguous AAC blocks of the right length and duration from the fixture's audio track. On a
generated mono PCM ramp, whose sample values encode their own positions, every decoded sample must
match its reported position after sample-accurate seeks, and mono must be duplicated to both
channels. `gb_telemetry_tests` also checks the shared nearest-record tie policy.
The offscreen `gb-player` smoke tests load the QML scene with the software scene graph and play
the fixture from 1.05 s. The first presented frame must be the 1.0 s frame of the dual-stream
layout, with telemetry correctly reported as absent.

`gb_player_audio_gating` plays a generated 10 s clip with tone through SDL's dummy audio driver
using the real benchmark. It fails if any audio frame is emitted while scrubbing, during a burst of
seeks while playing, or while paused, or if audio does not resume after each. A deliberately
broken gate was confirmed to fail this test.

GPU rendering, real-time smoothness, and seek latency need a display and real media. They are
measured with `gb-player --benchmark` and recorded under `research/findings/`, not in CTest.

The test suite also invokes all six tools' and `gb-player`'s `--help` and `--version`, generates isolated ISO BMFF,
GPMF, and JPEG CLI fixtures, checks all JSON schema IDs, and verifies that corrupt inputs fail.
Future work should add full JSON Schema validation, fuzzing, sanitizers, and curated public or
fingerprint-only regression cases derived from real camera evidence.
