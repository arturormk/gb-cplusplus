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
gb-frames --reconstruct --time 5 --hud full --output panorama-with-hud.png recording.360
gb-frames --stream 0 --time 5 --hud basic --output labeled-frame.png recording.360
```

`--time` accepts seconds or `HH:MM:SS[.fraction]` and is independently converted to each selected
stream's integer time base. `--pts` is intentionally limited to single-stream extraction because
different tracks can have different time bases. Existing outputs are not replaced unless
`--force` is supplied. Diagnostics go to stderr; `--json` writes `gb.frames.streams/v1` for
listing and `gb.frames/v1` for extraction.

Raw RGB24 is tightly packed in row-major order with no header. Keep the JSON report beside a raw
dump: it records geometry, byte interpretation, source pixel format, range, matrix/space,
transfer, primaries, chroma location, integer PTS, duration, and rational time base.

## Metadata HUD

`--hud full` burns the complete, fixed HUD into PNG, PPM, or RGB24 output. It always shows the
selected frame's media time, projection and coverage/FOV status, geometry, source stream and
timestamp, color metadata, and timed-metadata status. If the file has a GPMF track, the containing
metadata sample is synchronized to the actual decoded frame timestamp and every decoded field in
that sample is added. Repeated sensor records are selected at the frame's position within the
metadata sample. Gravity, magnetometer, accelerometer, gyroscope, and orientation vectors also
receive graphical XY direction gauges with their Z values.

`--hud basic` burns a compact label inside the existing image containing the source filename,
selected-frame video time, GPS latitude/longitude and altitude, UTC timestamp, and 2D and 3D
speeds. It selects the telemetry record nearest the actual decoded frame. When surrounding valid
GPS positions and nonzero 2D speed provide a motion vector, it also reports forward azimuth in
degrees (`0` north, `90` east, `180` south, `270` west). Missing values say `N/A`. Neither HUD mode
changes the output dimensions. A mode is mandatory; bare `--hud` is invalid.

Unavailable metadata is explicitly shown as unavailable; it is never displayed as zero. The HUD
is intentionally unconditional within each mode: there are no switches for hiding, moving, or
styling individual elements. On very small images, text is clipped to the available overlay area.
`--json` reports the HUD mode, selected metadata track/sample, and the number of fields and vector
gauges rendered.

## Reconstruction status

`--reconstruct` is a deterministic CPU reference for the original MAX 4096×1344 pseudo-EAC
packing. Track 1 supplies `LEFT / FRONT / RIGHT`; track 2 supplies `DOWN / BACK / TOP`. Each face
is 1344 pixels wide, with 64 retained pixels distributed through the two side-face regions. The
transform applies the MAX equal-angular cube mapping, correct face rotations, and linear blending
of wrapped side-face duplicates. It is intentionally not camera calibration, optical-flow seam
stitching, stabilization, or exposure matching.

On the fingerprinted private sample, both HEVC streams decode at identical requested PTS values.
The reference transform exposes a near-black `BACK` face in stream 5 of the fingerprinted sample.
A direct source-region measurement confirms that those pixels are black before reconstruction;
the tool does not hide or synthesize them. This is a limitation of that evidence recording, not a
gap in the six-face mapping.

## Requirements and limitations

Build/runtime decoding uses FFmpeg libraries (libavformat, libavcodec, libavutil, and libswscale).
The decoder available in the installed FFmpeg build determines codec support. Only software
decoding and RGB24 output are implemented; hardware acceleration, edit-list normalization,
fragmented input validation, batch extraction, and EAC output are not. Virtual-camera projection
is available in `gb-project`, not this extraction CLI.
The CPU panorama path favors reproducibility over interactive speed and accepts bounded output
dimensions only.
