# MAX (2025) `.360` container: next experiments

Status: **unverified; original sample prerequisite unavailable in this repository.**

1. Obtain owner-authorized short original recordings covering each relevant shooting mode. Record
   camera model, firmware, settings, chapter behavior, file size, and SHA-256 without publishing
   private media or GPS.
2. Build and run `gb-inspect --json sample.360 > sample.structure.json`; retain the exact tool
   commit and diagnostics.
3. Cross-check topology using a currently reviewed FFmpeg/`ffprobe` build, recording its full
   version/configuration and a command such as `ffprobe -v error -show_format -show_streams
   -show_data -of json sample.360`.
4. Compare top-level boxes, every stream ID/type/codec/time base, vendor boxes, multiple video
   tracks, audio, and data tracks. Treat extensions and FourCCs as observations, not meanings.
5. Extend `gb_container` with tested parsers for `trak` metadata and sample descriptions so
   `gb-inspect` can enumerate the topology without delegating to FFmpeg. Preserve unknown entries.
6. Locate candidate metadata samples without interpreting them, then compare with ExifTool and a
   separately license/API-reviewed official GPMF parser.
7. Publish a sanitized structural fingerprint and report discrepancies under `research/findings/`.

Do not upload samples or inferred GPS. Do not modify originals. Work on copies only when an
explicit extraction experiment requires output.
