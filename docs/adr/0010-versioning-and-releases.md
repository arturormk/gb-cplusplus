# ADR-0010: Versioning and releases

Status: Accepted for v0.1 (2026-10-30)

## Context

The M4/M5 player is usable, and the project is being published on GitHub as the v0.1 series.
Every build needs a version that identifies its source. Release versions must still be chosen
by a maintainer and never changed by tooling. All executables already print `PROJECT_VERSION`
for `--version`, and `gb-player` sends it in the map tile User-Agent.

## Decision

1. **Odd/even minors.** `MAJOR.MINOR` with an odd minor (0.1, 0.3, 1.1, …) is a development
   series. An even minor (0.2, 1.0, 1.2, …) is a release series.
2. **`VERSION` is the single source.** The top-level `VERSION` file holds `MAJOR.MINOR` for a
   development series. For a release series it holds `MAJOR.MINOR` or `MAJOR.MINOR.PATCH`.
   `cmake/GbVersion.cmake` reads it before `project()`, so `PROJECT_VERSION` and every
   `--version` output follow it.
3. **Development versions are derived automatically.** For odd minors the patch is
   `git rev-list --count HEAD`, the number of commits in the history of the build, e.g.
   `0.1.13`. The count only grows along a branch, and it never needs a commit to update it. A
   `VERSION` with a patch and an odd minor is rejected. CMake reconfigures when `.git/logs/HEAD`
   or `VERSION` changes, so incremental builds follow new commits.
4. **Release versions are manual.** For even minors `VERSION` is used verbatim (`1.0` stays
   `1.0`). Only a maintainer edits it, for example to `1.0.1` for a fix release.
5. **No-history builds.** Shallow clones undercount and produce a CMake warning; CI fetches full
   history. Builds without Git default the patch to 0 with a warning. Pass
   `-DGB_VERSION_PATCH_OVERRIDE=N` to set it.
6. **Tags and GitHub releases.** Tags are `v` plus the derived version (`v0.1.14`, `v1.0`). The
   `release` workflow refuses a tag that differs from `cmake -P cmake/GbVersion.cmake`. Only
   even-minor tags (`v1.0`, `v1.0.1`, `v1.2`) build, test and publish a GitHub release. Odd-minor
   tags only mark development versions and never produce release artifacts.
7. **Packaging follows even minors.** Even-minor releases also publish `.deb` and `.rpm`
   packages, built by CPack (`cmake/GbPackaging.cmake`) from the `release` preset. The `.deb` is
   built on Ubuntu 22.04 and the `.rpm` in a Fedora container, so each requires the library
   sonames and package names of its own family. The `release` workflow attaches both, with a
   `SHA256SUMS` file, to a **draft** release: nothing is public until a maintainer reviews the
   draft and publishes it on GitHub. A rerun refreshes the files of a draft and refuses to touch a
   published release. `./quickstart.sh` builds the same packages locally (amended 2026-11-02).

## Consequences

- A development version identifies a commit on the mainline. Builds of parallel branches can
  share a count, so bug reports from branches should also give the commit hash.
- Starting a release series is a one-line `VERSION` change in a commit and a `v0.2` tag on
  that commit. Returning to development (`0.3`) is another one-line change.
- The `release` workflow must not run with a shallow checkout. It uses `fetch-depth: 0`.
