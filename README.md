# gb-cplusplus

`gb-cplusplus` is the C++20/Linux implementation of an independent open-source toolkit for
advanced GoPro camera users. Its current laboratory tools inspect containers and GPMF, extract
timestamped GPS telemetry, and diagnose media/GPS clock relationships. GoBro is a provisional
public-facing name and is not affiliated with, endorsed by, or sponsored by GoPro.

The M0 baseline, M1 container/metadata laboratory, and M2 telemetry/timing laboratory are
complete; M3 native video reconstruction is next. Support claims remain limited to the
fingerprinted GoPro MAX (2025) samples documented under `research/` and do not yet include native
video reconstruction or playback.

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
`cmake`, and `ninja-build`, then run:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

No Qt, FFmpeg, camera SDK, network access, or private media is required for these milestones. See
[`docs/dependencies.md`](docs/dependencies.md) for exact current requirements.

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
```

The tool reads the source only, reports checked byte offsets and lengths, and never extracts or
modifies payloads. ISO BMFF JSON uses `gb.inspect/v1`; JPEG JSON uses `gb.inspect.jpeg/v1`.
Diagnostics go to stderr.

Metadata inventory JSON uses `gb.metadata/v1`; selected GPMF samples use
`gb.metadata.sample/v1`. See [`tools/gb-metadata/README.md`](tools/gb-metadata/README.md) for raw
payload export and current limitations.

Telemetry JSON uses `gb.telemetry/v1`; timing diagnostics use `gb.time/v1`. GPS exports preserve
integer media ticks, units, fix validity, DOP, provenance, gaps, and timing uncertainty. See the
individual tool READMEs for the policies used by GPX/GeoJSON and media-to-UTC mapping.

## Scope and project guidance

The implementation follows [`docs/BLUEPRINT.md`](docs/BLUEPRINT.md): tools establish reproducible
format evidence before shared functionality moves into a player. Current architecture, testing,
known limitations, and research procedure are documented under `docs/` and `research/`.

## Contributing

Keep changes focused on one laboratory capability. Add synthetic or legally distributable tests
for binary behavior, keep source media out of Git, and record new format findings with commands,
file fingerprints, and a clear confirmed-versus-hypothesis label. Before submitting a change,
build with Ninja and run `ctest --preset dev`. The project license is not final; do not contribute
code copied from third-party projects or add dependencies without a license review.
