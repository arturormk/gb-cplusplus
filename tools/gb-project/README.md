# gb-project

`gb-project` is the Qt 6 visual projection laboratory for native two-stream GoPro MAX `.360`
media. It opens the original recording read-only, selects synchronized frames at a requested media
time, and shows four diagnostic views:

1. raw track 1 (`LEFT / FRONT / RIGHT` packed EAC faces);
2. raw track 2 (`DOWN / BACK / TOP` packed EAC faces);
3. the reconstructed 2:1 equirectangular panorama; and
4. an interactive virtual-camera view.

## Use

```sh
gb-project recording.360
gb-project --time 5 --width 2048 recording.360
gb-project --screenshot /tmp/projection-lab.png --time 5 recording.360
```

`--time` currently accepts non-negative seconds. The toolbar can reopen the selected file at a
different timestamp and choose a 1024, 2048, 4096, or 5376-pixel panorama. Drag the virtual-camera
view to change yaw/pitch, use the mouse wheel for field of view, or use the explicit yaw, pitch,
roll, and FOV sliders. Available output mappings are rectilinear, equidistant fisheye,
stereographic, and Little Planet. **Save CPU reference** writes a 1920×1080 PNG, PPM, or RGB24
view using the deterministic reference implementation rather than a screen capture.

The interactive view uses a Qt Quick GPU shader when the scene-graph backend supports custom
shaders. A CPU-rendered reference remains visible on software backends and refreshes when a
control gesture ends. This gives the laboratory a responsive path and a testable independent path
using the same projection conventions.

## Reconstruction model

For the verified 4096×1344 MAX layout, each strip contains three 1344-pixel EAC cube faces plus
64 pixels of retained stitch overlap. Track 1 supplies `LEFT`, `FRONT`, and `RIGHT`; track 2
supplies `DOWN`, `BACK`, and `TOP`. Side faces contain wrapped duplicate regions which are linearly
blended. This replaces the earlier incorrect assumption that the two tracks were simply the top
and bottom rows of a generic cube atlas.

The fingerprinted private sample has an essentially black `BACK` face in the center of track 2;
the GUI reports its measured source luma and preserves it. The black region is therefore not
introduced or synthesized by reconstruction. Other faces, orientation, overlap, wrap continuity,
and virtual-camera directions are covered by deterministic fixtures and real-sample visual
checks.

## Current scope

This is a timestamp-oriented projection laboratory, not the M4 media player. It does not provide
continuous playback, audio, stabilization, optical-flow seam correction, calibration recovery,
or telemetry/map panels. It requires exactly two video streams with the verified MAX EAC strip
geometry. Original recordings are never modified.
