# Dependencies

## Required now (M0 through M4)

The supported baseline is Ubuntu 22.04 or newer with:

- a C++20 compiler: GCC 11.4+ or Clang 14+;
- CMake 3.22+;
- Ninja 1.10+; and
- pkg-config; and
- FFmpeg 4.4+ development/runtime packages providing libavformat 58+, libavcodec 58+, libavutil
  56+, and libswscale 5+; and
- SDL 2.0.10+ development package (`libsdl2-dev`), used by `gb-player` for audio output only; and
- Qt 6.2+ development packages providing Core, Gui, QML, Quick, Widgets, and Shader Tools, plus
  the Qt Quick Controls/QML runtime modules used by `gb-project`; and
- the C++ standard library supplied with the compiler.

Exact Ubuntu packages:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config ffmpeg \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libsdl2-dev \
  qt6-base-dev qt6-declarative-dev libqt6shadertools6-dev qt6-shader-baker \
  qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-nativestyle qml6-module-qtquick-templates qml6-module-qtquick-window
```

The build has no downloaded dependencies. The terminal tools dynamically use the packaged FFmpeg
libraries. `gb-project` and `gb-player` also use the packaged Qt 6 libraries and QML modules.
The M4 video baseline added no packages. M4 audio adds SDL2: `gb-player` uses only its audio
subsystem, with signal handlers disabled. SDL2 is zlib-licensed. `gb-player` issues OpenGL calls through `QOpenGLExtraFunctions` from Qt GUI
and does not need the Qt OpenGL module. At runtime it needs an OpenGL 3.3 or OpenGL ES 3.0 driver
to display video; the Mesa drivers in Ubuntu 22.04 provide this.
The versions above match the initial verified development environment; CI should continuously
exercise the oldest supported baseline.

CI uses
[`actions/checkout` v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1), pinned to
upstream commit `3d3c42e5aac5ba805825da76410c181273ba90b1` rather than a mutable tag. It is CI
infrastructure only and is not a build or runtime dependency of `gb-inspect`.

## Explicitly not required yet

ExifTool, the GoPro GPMF parser, and map/rendering libraries are not runtime dependencies. FFmpeg
and Qt are required by the current all-target build, but remain unnecessary when building the
earlier library/tool targets in isolation.

The project license remains under review as directed by the blueprint. No third-party source code
is vendored. `gb_media` links the system FFmpeg shared libraries and `gb-project` links the system
Qt 6 libraries; binary redistribution must review the selected builds and their applicable
LGPL/GPL or commercial-license obligations.

## FFmpeg review

The implementation is compiled and tested against Ubuntu 22.04 FFmpeg 4.4.2. It uses only public
libavformat/libavcodec/libavutil/libswscale APIs and the packaged PNG/HEVC/MPEG-4 decoders or
encoder exercised by tests and private validation. The 2024 `gopromax_opencl` proposal was
reviewed as format research; it is not present in that build and no FFmpeg source is vendored.
Primary references reviewed: the
[FFmpeg v4 filter proposal](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-August/331984.html),
[GoPro's EAC packing description](https://gopro.com/en/ca/news/max-tech-specs-stitching-resolution),
and the Apache-2.0 [max2sphere reference](https://github.com/trek-view/max2sphere).

## GPMF reference review

M1's native parser and M2's GPS5/GPSF/GPSP/GPSU interpretation were checked against the official
GoPro GPMF parser at commit
[`9a7150632892c7356c91145c889016f07b0ed48d`](https://github.com/gopro/gpmf-parser/commit/9a7150632892c7356c91145c889016f07b0ed48d),
reviewed 2026-10-29. The reference reports parser version 2.1.0 in `GPMF_common.h` and is offered
under MIT or Apache-2.0. It is a format/behavior reference only: no source is vendored, linked, or
required to build. FFprobe 4.4.2 and ExifTool 12.40 remain optional independent diagnostic tools.
