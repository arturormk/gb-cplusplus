# gb-cplusplus

[![ci](https://github.com/arturormk/gb-cplusplus/actions/workflows/ci.yml/badge.svg)](https://github.com/arturormk/gb-cplusplus/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)
[![Built with Claude Code](https://img.shields.io/badge/built_with-Claude_Code-D97757?logo=claude&logoColor=white)](https://claude.com/claude-code)
[![tools/ built with Codex](https://img.shields.io/badge/tools%2F_built_with-Codex-412991?logo=openai&logoColor=white)](https://openai.com/codex)

`gb-cplusplus` is the C++20/Linux implementation of an independent open-source toolkit for
advanced GoPro camera users. Its current laboratory tools inspect containers and GPMF, extract
timestamped GPS telemetry, and diagnose media/GPS clock relationships. GoBro is a provisional
public-facing name and is not affiliated with, endorsed by, or sponsored by GoPro.

The M0 baseline, M1 container/metadata laboratory, M2 telemetry/timing laboratory, and M3 native
video reconstruction/projection laboratory are complete, and the M4 player baseline and M5 map
have met their gates. `gb-frames` provides exact native frame extraction and deterministic reconstruction, and
`gb-project` provides the Qt visual comparison and interactive virtual camera. `gb-player` plays
original MAX `.360` recordings directly, with GPU spherical viewing, accurate seeking, synchronized
telemetry and metadata panels, synchronized audio that plays only during stable playback (never
while scrubbing or seeking), a live OpenStreetMap route map with click-to-seek, and performance
diagnostics.

## Build and test

For the quickest setup, run:

```sh
./quickstart.sh
```

The script detects missing Ubuntu build packages, installs them through
`apt-get`, builds and tests the project, installs user-facing binaries under `build/dev/install`,
and prints their paths relative to the directory from which it was invoked. Use
`./quickstart.sh --help` for automation-friendly options.

To run the individual build steps manually on Ubuntu 22.04 or newer, install `build-essential`,
`cmake`, `ninja-build`, `pkg-config`, `ffmpeg`, and the FFmpeg/Qt development packages listed in
[`docs/dependencies.md`](docs/dependencies.md), then run:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

No camera SDK, network access, or private media is required to build and test. FFmpeg and Qt 6 are required for the
M3 tools; see [`docs/dependencies.md`](docs/dependencies.md) for exact versions and packages.

## Use

```sh
build/dev/tools/gb-inspect/gb-inspect recording.360
build/dev/tools/gb-inspect/gb-inspect --json recording.360
build/dev/tools/gb-inspect/gb-inspect --help
build/dev/tools/gb-metadata/gb-metadata recording.360
build/dev/tools/gb-metadata/gb-metadata --sample 0 --key GPS5 recording.360
build/dev/tools/gb-metadata/gb-metadata --values --key GPS5 recording.360
build/dev/tools/gb-metadata/gb-metadata --json --sample 0 recording.360
build/dev/tools/gb-telemetry/gb-telemetry --format json recording.360
build/dev/tools/gb-telemetry/gb-telemetry --format gpx recording.360 > route.gpx
build/dev/tools/gb-time/gb-time --json --at 5000 recording.360
build/dev/tools/gb-frames/gb-frames --list --json recording.360
build/dev/tools/gb-frames/gb-frames --stream 0 --time 5 --output stream-0.png recording.360
build/dev/tools/gb-frames/gb-frames --reconstruct --stream 0 --stream 5 \
  --time 5 --output panorama.png recording.360
build/dev/tools/gb-project/gb-project --time 5 recording.360
build/dev/apps/gb-player/gb-player recording.360
build/dev/apps/gb-player/gb-player --map-no-network recording.360
build/dev/apps/gb-player/gb-player --benchmark 60 --seeks 20 recording.360 > benchmark.json
```

Source media is opened read-only; generated reports, frames, and projections are written only to
requested output paths. ISO BMFF JSON uses `gb.inspect/v1`; JPEG JSON uses
`gb.inspect.jpeg/v1`. Diagnostics go to stderr.

Metadata inventory JSON uses `gb.metadata/v1`; selected GPMF samples use
`gb.metadata.sample/v1`. See [`tools/gb-metadata/README.md`](tools/gb-metadata/README.md) for raw
payload export and current limitations.

Telemetry JSON uses `gb.telemetry/v1`; timing diagnostics use `gb.time/v1`. GPS exports preserve
integer media ticks, units, fix validity, DOP, provenance, gaps, and timing uncertainty. See the
individual tool READMEs for the policies used by GPX/GeoJSON and media-to-UTC mapping.

Frame reports use `gb.frames.streams/v1` and `gb.frames/v1`, retaining integer PTS/time bases and
source color metadata. The face-specific MAX EAC reconstruction and virtual-camera projection are
shared by `gb-frames`, `gb-project`, and `gb-player`; see their individual READMEs for controls and
limitations. Player benchmark reports use `gb.player.benchmark/v1`; see
[`apps/gb-player/README.md`](apps/gb-player/README.md).

When a recording has GPS, `gb-player` requests OpenStreetMap tiles for the visible map area.
This reveals the viewed area to the tile server, but never the recording or its track. Tiles are
cached in `~/.cache/gb/tiles` (or `$XDG_CACHE_HOME/gb/tiles`) and can be deleted at any time. Use
`--map-no-network` to show cached tiles only. Map data © OpenStreetMap contributors.

## Scope and project guidance

The implementation follows [`docs/BLUEPRINT.md`](docs/BLUEPRINT.md): tools establish reproducible
format evidence before shared functionality moves into a player. Current architecture, testing,
known limitations, and research procedure are documented under `docs/` and `research/`.

## AI provenance

This is an AI-generated project directed and curated by a human. The M0–M3 laboratory tools
under `tools/` and the libraries they introduced were developed with OpenAI Codex. `gb-player`
(`apps/`) and the M4–M5 library work, including `gb-geo`, `gb-tiles`, and audio decoding, were
developed with Claude Code. The commit history and the decision records in `docs/adr/`
document the work.

## Versioning

The version comes from [`VERSION`](VERSION) and Git; every tool prints it with `--version`, and
`cmake -P cmake/GbVersion.cmake` prints it without building. Odd minors (0.1, 0.3, …) are
development series whose patch is the commit count, e.g. `0.1.13`. Even minors (0.2, 1.0, …)
are manually versioned releases. See [ADR-0010](docs/adr/0010-versioning-and-releases.md) and
[`CHANGELOG.md`](CHANGELOG.md).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Report security issues privately as described in
[`SECURITY.md`](SECURITY.md).

## License

Copyright (C) 2026 Arturo R Montesinos and contributors.

gb-cplusplus is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
[`LICENSE`](LICENSE).

The tools link the system FFmpeg, Qt 6, and SDL2 libraries; see
[`docs/dependencies.md`](docs/dependencies.md) for their licenses. GoBro and gb-cplusplus are
not affiliated with, endorsed by, or sponsored by GoPro.
