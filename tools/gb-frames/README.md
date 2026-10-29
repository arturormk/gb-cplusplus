# gb-frames

`gb-frames` lists and decodes video streams directly from a source media file. It selects the
nearest decoded presentation frame at an integer stream timestamp (choosing the earlier frame on
an exact tie), writes PNG, PPM, or packed RGB24, and reports the requested/selected ticks and
source color metadata. Original media is opened read-only.

## Examples

```sh
gb-frames --list --json recording.360
gb-frames --stream 0 --time 00:00:05 --output front.png recording.360
gb-frames --stream 5 --pts 450000 --format rgb24 --output rear.rgb recording.360 \
  --json > rear.json
gb-frames --reconstruct --stream 0 --stream 5 --time 5 \
  --width 4096 --height 2048 --output panorama.png recording.360
```

`--time` accepts seconds or `HH:MM:SS[.fraction]` and is independently converted to each selected
stream's integer time base. `--pts` is intentionally limited to single-stream extraction because
different tracks can have different time bases. Existing outputs are not replaced unless
`--force` is supplied. Diagnostics go to stderr; `--json` writes `gb.frames.streams/v1` for
listing and `gb.frames/v1` for extraction.

Raw RGB24 is tightly packed in row-major order with no header. Keep the JSON report beside a raw
dump: it records geometry, byte interpretation, source pixel format, range, matrix/space,
transfer, primaries, chroma location, integer PTS, duration, and rational time base.

## Reconstruction status

`--reconstruct` is a deterministic CPU reference for the original MAX 4096×1344 pseudo-EAC
packing. It maps the two strips to an equirectangular image and linearly blends the configured
overlap (64 pixels by default). It is intentionally not camera calibration, optical-flow seam
stitching, stabilization, or exposure matching.

On the fingerprinted private sample, both HEVC streams decode at identical requested PTS values.
The reference transform also exposes a near-black center face in stream 5, producing a missing
BACK region. The tool does not hide or synthesize that region. Until its cause and the required
mode/calibration interpretation are established, the M3 navigable-panorama gate remains open.

## Requirements and limitations

Build/runtime decoding uses FFmpeg libraries (libavformat, libavcodec, libavutil, and libswscale).
The decoder available in the installed FFmpeg build determines codec support. Only software
decoding and RGB24 output are implemented; hardware acceleration, edit-list normalization,
fragmented input validation, batch extraction, EAC output, and virtual-camera projection are not.
The CPU panorama path favors reproducibility over interactive speed and accepts bounded output
dimensions only.
