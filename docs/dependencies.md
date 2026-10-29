# Dependencies

## Required now (M0/M1/M2 and the `gb-frames` M3 slice)

The supported baseline is Ubuntu 22.04 or newer with:

- a C++20 compiler: GCC 11.4+ or Clang 14+;
- CMake 3.22+;
- Ninja 1.10+; and
- pkg-config; and
- FFmpeg 4.4+ development/runtime packages providing libavformat 58+, libavcodec 58+, libavutil
  56+, and libswscale 5+; and
- the C++ standard library supplied with the compiler.

Exact Ubuntu packages:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config ffmpeg \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

The build has no downloaded dependencies. The M0-M2 tools retain no non-system runtime
dependency; `gb-frames` dynamically uses the packaged FFmpeg libraries.
The versions above match the initial verified development environment; CI should continuously
exercise the oldest supported baseline.

CI uses
[`actions/checkout` v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1), pinned to
upstream commit `3d3c42e5aac5ba805825da76410c181273ba90b1` rather than a mutable tag. It is CI
infrastructure only and is not a build or runtime dependency of `gb-inspect`.

## Explicitly not required yet

Qt, ExifTool, the GoPro GPMF parser, and map/rendering libraries are not runtime dependencies.
FFmpeg is required by the current all-target build for `gb-frames`, but remains unnecessary when
building the earlier library/tool targets in isolation.

The project license remains under review as directed by the blueprint. No third-party source code
is vendored. `gb_media` links the system FFmpeg shared libraries; binary redistribution must
review the selected FFmpeg build configuration and LGPL/GPL obligations.

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
