#!/usr/bin/env bash
# Formats the project's tracked C++ sources with the pinned clang-format version.
#
#   scripts/format.sh          rewrite files in place
#   scripts/format.sh --check  change nothing; exit 1 and list the files that need formatting
#
# The version is pinned because each clang-format release formats some code differently; CI and
# quickstart.sh use the same one. Style rules live in .clang-format.

set -Eeuo pipefail

readonly CLANG_FORMAT="${CLANG_FORMAT:-clang-format-15}"
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

check=false
case "${1:-}" in
  "") ;;
  --check) check=true ;;
  -h | --help)
    sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  *)
    printf 'format.sh: unknown option: %s\n' "$1" >&2
    exit 2
    ;;
esac

if ! command -v "${CLANG_FORMAT}" >/dev/null 2>&1; then
  printf 'format.sh: %s not found; install it with: sudo apt-get install %s\n' \
    "${CLANG_FORMAT}" "${CLANG_FORMAT}" >&2
  exit 2
fi

cd -- "${SCRIPT_DIR}/.."
mapfile -d '' files < <(git ls-files -z -- '*.cpp' '*.hpp' '*.h' '*.cc')
if ((${#files[@]} == 0)); then
  printf 'format.sh: no C++ sources found\n' >&2
  exit 2
fi

if [[ "${check}" != true ]]; then
  "${CLANG_FORMAT}" -i -- "${files[@]}"
  printf 'Formatted %d files with %s.\n' "${#files[@]}" "$("${CLANG_FORMAT}" --version)"
  exit 0
fi

unformatted=()
for file in "${files[@]}"; do
  if ! "${CLANG_FORMAT}" --dry-run --Werror -- "${file}" >/dev/null 2>&1; then
    unformatted+=("${file}")
  fi
done
if ((${#unformatted[@]} > 0)); then
  printf '%d of %d files need formatting (run scripts/format.sh):\n' \
    "${#unformatted[@]}" "${#files[@]}" >&2
  printf '  %s\n' "${unformatted[@]}" >&2
  exit 1
fi
printf 'All %d C++ files are formatted.\n' "${#files[@]}"
