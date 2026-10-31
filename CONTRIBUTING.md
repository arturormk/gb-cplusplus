# Contributing to gb-cplusplus

Thank you for your interest. Keep each change focused on one laboratory capability or player
feature. The [blueprint](docs/BLUEPRINT.md) sets the order of work: tools establish reproducible
format evidence before shared functionality moves into the player.

## Before you open a pull request

- Build with Ninja and run the tests:

  ```sh
  cmake --preset dev
  cmake --build --preset dev
  ctest --preset dev
  ```

- Format C++ sources with clang-format 15 (`sudo apt-get install clang-format-15`). The
  rules are in `.clang-format`; the version is pinned because releases format differently.

  ```sh
  scripts/format.sh          # rewrite in place
  scripts/format.sh --check  # report only
  ```

  CI fails when a file is not formatted. Put pure reformatting in its own commit and add its
  hash to `.git-blame-ignore-revs`; run `git config blame.ignoreRevsFile .git-blame-ignore-revs`
  once so local `git blame` skips those commits too.

- Add synthetic or legally distributable tests for binary behavior. Keep source media
  (`.360`, `.mp4`, `.jpg`, …) and generated outputs out of Git.
- Record new format findings under `research/findings/`. Include the commands, file
  fingerprints, and a clear confirmed-or-hypothesis label. Do not publish GPS positions,
  camera serial numbers, or other personal data from your recordings.
- Record architectural decisions as ADRs in `docs/adr/`.
- Use [Conventional Commits](https://www.conventionalcommits.org/) subjects, e.g.
  `feat(gb-player): …` or `fix(gb-media): …`.

## Licensing

gb-cplusplus is licensed under the [GNU General Public License v3.0 or later](LICENSE). By
contributing, you agree that your contribution is licensed under the same terms. Do not
contribute code copied from third-party projects. Do not add a dependency without documenting
its license in [`docs/dependencies.md`](docs/dependencies.md).

## Versioning

Do not edit `VERSION` in feature changes. Maintainers change it to start a release (even
minor) or a development series (odd minor); see
[ADR-0010](docs/adr/0010-versioning-and-releases.md).

## Trademarks

GoBro is a provisional name. The project is not affiliated with, endorsed by, or sponsored by
GoPro. Do not add GoPro logos or wording that implies endorsement.
