# Dependencies

## Required now (M0/M1 laboratory tools)

The supported baseline is Ubuntu 22.04 or newer with:

- a C++20 compiler: GCC 11.4+ or Clang 14+;
- CMake 3.22+;
- Ninja 1.10+; and
- the C++ standard library supplied with the compiler.

Exact Ubuntu packages:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build
```

The build has no downloaded dependencies and the executable has no non-system runtime dependency.
The versions above match the initial verified development environment; CI should continuously
exercise the oldest supported baseline.

CI uses
[`actions/checkout` v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1), pinned to
upstream commit `3d3c42e5aac5ba805825da76410c181273ba90b1` rather than a mutable tag. It is CI
infrastructure only and is not a build or runtime dependency of `gb-inspect`.

## Explicitly not required yet

Qt, FFmpeg, ExifTool, the GoPro GPMF parser, and map/rendering libraries are candidates for later
milestones, not M0 dependencies. Their current APIs, versions, build requirements, enabled
features, and licenses must be reviewed and recorded before they enter the build. In particular,
Qt/FFmpeg packages should not be installed merely to build `gb-inspect`.

The project license remains under review as directed by the blueprint. No third-party source code
is currently vendored or linked.

## GPMF reference review

M1's native parser was checked against the official GoPro GPMF parser at commit
[`9a7150632892c7356c91145c889016f07b0ed48d`](https://github.com/gopro/gpmf-parser/commit/9a7150632892c7356c91145c889016f07b0ed48d),
reviewed 2026-10-29. The reference reports parser version 2.1.0 in `GPMF_common.h` and is offered
under MIT or Apache-2.0. It is a format/behavior reference only: no source is vendored, linked, or
required to build. FFprobe 4.4.2 and ExifTool 12.40 remain optional independent diagnostic tools.
