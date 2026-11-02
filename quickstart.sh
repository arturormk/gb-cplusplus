#!/usr/bin/env bash

set -Eeuo pipefail

readonly INVOCATION_DIR="$(pwd -P)"
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly PRESET="dev"
readonly BUILD_DIR="${SCRIPT_DIR}/build/${PRESET}"
readonly INSTALL_DIR="${BUILD_DIR}/install"
readonly PACKAGE_PRESET="release"
readonly PACKAGE_BUILD_DIR="${SCRIPT_DIR}/build/${PACKAGE_PRESET}"
readonly PACKAGE_DIR="${PACKAGE_BUILD_DIR}/packages"
readonly PACKAGE_NAME="gb-cplusplus"
# Pinned so every machine formats identically; exported so scripts/format.sh uses the same one.
declare -rx CLANG_FORMAT="clang-format-15"

install_dependencies=true
run_tests=true
check_format=true
package_formats=""      # empty: ask; "none", "deb", "rpm" or "deb rpm"
install_package=""      # empty: ask; true or false

usage() {
  cat <<'EOF'
Usage: ./quickstart.sh [options]

Configure, build, test, and locally install gb-cplusplus, then check that the
C++ sources are formatted.

Options:
  --no-install-deps  Do not try to install missing Ubuntu packages
  --skip-tests       Build without running the test suite
  --skip-format      Do not check C++ formatting
  --package FORMATS  Also build release packages: deb, rpm, deb,rpm or none
  --install-package  Install (or reinstall) the package for this system's format
  --no-install-package
                     Do not install the package
  -h, --help         Show this help and exit

Missing build tools are installed with apt-get when it is available. The script
uses sudo when needed and may prompt for the user's password. Build products stay
inside build/dev. A formatting problem is reported as a warning and does not fail
the build; fix it with scripts/format.sh.

Packages: when run from a terminal without --package, the script asks whether to
build .deb and/or .rpm packages. They come from a separate Release build in
build/release and are written to build/release/packages. When this system uses
the chosen format (dpkg/apt or rpm/dnf/zypper), the script also asks whether to
install the package system-wide. An install over an older or identical version
replaces it, and files of the old package that the new one no longer contains
are removed. Without a terminal and without these options, no package is built
or installed.
EOF
}

while (($# > 0)); do
  case "$1" in
    --no-install-deps)
      install_dependencies=false
      ;;
    --skip-tests)
      run_tests=false
      ;;
    --skip-format)
      check_format=false
      ;;
    --package | --package=*)
      if [[ "$1" == --package=* ]]; then
        formats_argument="${1#--package=}"
      else
        if (($# < 2)); then
          printf 'quickstart.sh: --package needs a value\n' >&2
          exit 2
        fi
        formats_argument="$2"
        shift
      fi
      package_formats=""
      IFS=', ' read -r -a requested_formats <<<"${formats_argument}"
      for format in "${requested_formats[@]}"; do
        case "${format}" in
          deb | rpm) [[ " ${package_formats} " == *" ${format} "* ]] || package_formats+=" ${format}" ;;
          none) ;;
          *)
            printf 'quickstart.sh: unknown package format: %s\n' "${format}" >&2
            exit 2
            ;;
        esac
      done
      package_formats="${package_formats# }"
      [[ -n "${package_formats}" ]] || package_formats="none"
      ;;
    --install-package)
      install_package=true
      ;;
    --no-install-package)
      install_package=false
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      printf 'quickstart.sh: unknown option: %s\n\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

# The package format this system installs natively, if any.
native_format=""
if command -v dpkg-query >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then
  native_format="deb"
elif command -v rpm >/dev/null 2>&1 &&
     { command -v dnf >/dev/null 2>&1 || command -v zypper >/dev/null 2>&1 ||
       command -v yum >/dev/null 2>&1; }; then
  native_format="rpm"
fi

wants_format() {
  [[ " ${package_formats} " == *" $1 "* ]]
}

# Runs a command as root, through sudo when needed.
as_root() {
  if ((EUID == 0)); then
    "$@"
  else
    sudo "$@"
  fi
}

# Prints the installed version of the package in the native format, or nothing.
installed_version() {
  if [[ "${native_format}" == deb ]]; then
    { dpkg-query -W -f '${Status} ${Version}' "${PACKAGE_NAME}" 2>/dev/null || true; } |
      sed -n 's/^install ok installed //p'
  else
    rpm -q --qf '%{VERSION}-%{RELEASE}' "${PACKAGE_NAME}" 2>/dev/null || true
  fi
}

# Prints the non-directory paths the installed package owns, one per line, sorted.
installed_files() {
  local path
  if [[ "${native_format}" == deb ]]; then
    dpkg-query -L "${PACKAGE_NAME}" 2>/dev/null || true
  else
    rpm -ql "${PACKAGE_NAME}" 2>/dev/null | grep '^/' || true
  fi | while IFS= read -r path; do
    if [[ -n "${path}" && ! -d "${path}" ]]; then
      printf '%s\n' "${path}"
    fi
  done | LC_ALL=C sort -u
}

# Succeeds when some installed package owns the path.
path_is_owned() {
  if [[ "${native_format}" == deb ]]; then
    dpkg-query -S "$1" >/dev/null 2>&1
  else
    rpm -qf "$1" >/dev/null 2>&1
  fi
}

# Installs, upgrades, downgrades or reinstalls the package file with the system package manager.
install_package_file() {
  local file="$1" current new
  if [[ "${native_format}" == deb ]]; then
    # apt resolves dependencies; --reinstall covers an identical version.
    as_root apt-get install -y --reinstall --allow-downgrades "${file}"
    return
  fi
  current="$(installed_version)"
  new="$(rpm -qp --qf '%{VERSION}-%{RELEASE}' "${file}")"
  if command -v dnf >/dev/null 2>&1; then
    if [[ -z "${current}" ]]; then
      as_root dnf install -y "${file}"
    elif [[ "${current}" == "${new}" ]]; then
      as_root dnf reinstall -y "${file}"
    elif [[ "$(printf '%s\n%s\n' "${current}" "${new}" | sort -V | head -n1)" == "${new}" ]]; then
      as_root dnf downgrade -y "${file}"
    else
      as_root dnf upgrade -y "${file}"
    fi
  elif command -v zypper >/dev/null 2>&1; then
    as_root zypper --non-interactive install --oldpackage --force "${file}"
  else
    as_root rpm -Uvh --replacepkgs --oldpackage "${file}"
  fi
}

if [[ -z "${package_formats}" ]]; then
  package_formats="none"
  if [[ -t 0 ]]; then
    while true; do
      read -r -p 'Build installable packages too? [n]one, [d]eb, [r]pm, [b]oth (default: none): ' answer
      case "${answer,,}" in
        '' | n | none) package_formats="none"; break ;;
        d | deb) package_formats="deb"; break ;;
        r | rpm) package_formats="rpm"; break ;;
        b | both) package_formats="deb rpm"; break ;;
        *) printf 'Please answer n, d, r or b.\n' ;;
      esac
    done
  fi
fi

if [[ -n "${native_format}" ]] && wants_format "${native_format}"; then
  if [[ -z "${install_package}" ]]; then
    install_package=false
    if [[ -t 0 ]]; then
      current_version="$(installed_version)"
      if [[ -n "${current_version}" ]]; then
        prompt="Reinstall ${PACKAGE_NAME} (${current_version} is installed) from the new .${native_format}? [y/N]: "
      else
        prompt="Install the new .${native_format} package system-wide once it is built? [y/N]: "
      fi
      read -r -p "${prompt}" answer
      [[ "${answer,,}" == y || "${answer,,}" == yes ]] && install_package=true
    fi
  fi
elif [[ "${install_package}" == true ]]; then
  if [[ -z "${native_format}" ]]; then
    printf 'quickstart.sh: cannot tell whether this system installs .deb or .rpm packages.\n' >&2
  else
    printf 'quickstart.sh: --install-package needs --package %s on this system.\n' "${native_format}" >&2
  fi
  exit 2
fi

missing_packages=()
command -v c++ >/dev/null 2>&1 || missing_packages+=(build-essential)
command -v cmake >/dev/null 2>&1 || missing_packages+=(cmake)
command -v ninja >/dev/null 2>&1 || missing_packages+=(ninja-build)
command -v pkg-config >/dev/null 2>&1 || missing_packages+=(pkg-config)
command -v ffmpeg >/dev/null 2>&1 || missing_packages+=(ffmpeg)
if [[ "${check_format}" == true ]]; then
  command -v "${CLANG_FORMAT}" >/dev/null 2>&1 || missing_packages+=("${CLANG_FORMAT}")
fi
if ! command -v pkg-config >/dev/null 2>&1 ||
   ! pkg-config --exists libavformat libavcodec libavfilter libavutil libswscale; then
  missing_packages+=(libavformat-dev libavcodec-dev libavfilter-dev libavutil-dev libswscale-dev)
fi
if ! pkg-config --exists sdl2 2>/dev/null; then
  missing_packages+=(libsdl2-dev)
fi
if wants_format rpm && ! command -v rpmbuild >/dev/null 2>&1; then
  missing_packages+=(rpm)
fi
if wants_format deb && ! command -v dpkg-deb >/dev/null 2>&1; then
  missing_packages+=(dpkg-dev)
fi
if ! compgen -G '/usr/lib/*/cmake/Qt6/Qt6Config.cmake' >/dev/null ||
   ! compgen -G '/usr/lib/*/cmake/Qt6OpenGL/Qt6OpenGLConfig.cmake' >/dev/null ||
   ! compgen -G '/usr/lib/*/cmake/Qt6ShaderTools/Qt6ShaderToolsConfig.cmake' >/dev/null ||
   ! compgen -G '/usr/lib/*/qt6/qml/QtQuick/Controls/qmldir' >/dev/null ||
   [[ ! -x /usr/lib/qt6/bin/qsb ]]; then
  missing_packages+=(qt6-base-dev libqt6opengl6-dev qt6-declarative-dev
    libqt6shadertools6-dev qt6-shader-baker
    qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts
    qml6-module-qtquick-nativestyle qml6-module-qtquick-templates qml6-module-qtquick-window)
fi

if ((${#missing_packages[@]} > 0)); then
  printf 'Missing required build packages: %s\n' "${missing_packages[*]}"

  if [[ "${install_dependencies}" != true ]]; then
    printf 'Install them and rerun this script, or omit --no-install-deps.\n' >&2
    exit 1
  fi
  if ! command -v apt-get >/dev/null 2>&1; then
    printf 'Automatic installation currently supports apt-get-based systems only.\n' >&2
    printf 'Install a C++20 compiler, CMake 3.22+, Ninja 1.10+, pkg-config, FFmpeg development libraries, Qt 6 development/QML packages, clang-format 15, and rpmbuild or dpkg-deb when building packages, then rerun.\n' >&2
    exit 1
  fi

  if ((EUID != 0)) && ! command -v sudo >/dev/null 2>&1; then
    printf 'sudo is required to install missing packages.\n' >&2
    exit 1
  fi

  printf 'Installing missing build dependencies with apt-get...\n'
  as_root apt-get update
  as_root apt-get install -y --no-install-recommends "${missing_packages[@]}"
fi

printf 'Configuring with CMake preset %s...\n' "${PRESET}"
cd -- "${SCRIPT_DIR}"
cmake --preset "${PRESET}" "-UQt6*_DIR" -U CMAKE_PREFIX_PATH

printf 'Building all configured targets...\n'
cmake --build --preset "${PRESET}"

if [[ "${run_tests}" == true ]]; then
  printf 'Running tests...\n'
  ctest --preset "${PRESET}" --output-on-failure
fi

if [[ "${check_format}" == true ]]; then
  printf 'Checking C++ formatting...\n'
  if ! "${SCRIPT_DIR}/scripts/format.sh" --check; then
    printf 'Warning: formatting check failed (see above); the build itself succeeded.\n' >&2
  fi
fi

printf 'Installing user-facing executables locally...\n'
cmake --install "${BUILD_DIR}" --prefix "${INSTALL_DIR}"

mapfile -d '' executables < <(
  find "${INSTALL_DIR}/bin" -maxdepth 1 -type f -perm -u+x -print0 2>/dev/null | sort -z
)

if ((${#executables[@]} == 0)); then
  printf 'Build completed, but no installed executable targets were found.\n' >&2
  exit 1
fi

printf '\nBuild and local installation completed successfully.\n'
printf 'Executable files (relative to %s):\n' "${INVOCATION_DIR}"
for executable in "${executables[@]}"; do
  printf '  %s\n' "$(realpath --relative-to="${INVOCATION_DIR}" -- "${executable}")"
done

if [[ "${package_formats}" == none ]]; then
  exit 0
fi

printf '\nConfiguring the release build for packaging with CMake preset %s...\n' "${PACKAGE_PRESET}"
cmake --preset "${PACKAGE_PRESET}" "-UQt6*_DIR" -U CMAKE_PREFIX_PATH

printf 'Building the release build...\n'
cmake --build --preset "${PACKAGE_PRESET}"

cpack_generators=()
wants_format deb && cpack_generators+=(DEB)
wants_format rpm && cpack_generators+=(RPM)
# Start empty so only packages of the current version are left to install.
rm -rf -- "${PACKAGE_DIR}"
printf 'Building packages: %s...\n' "${package_formats}"
(IFS=';'; cpack --config "${PACKAGE_BUILD_DIR}/CPackConfig.cmake" \
  -G "${cpack_generators[*]}" -B "${PACKAGE_DIR}")

package_files=()
for format in ${package_formats}; do
  mapfile -t found < <(find "${PACKAGE_DIR}" -maxdepth 1 -type f -name "*.${format}" | sort)
  if ((${#found[@]} != 1)); then
    printf 'Expected one .%s package in %s, found %d.\n' "${format}" "${PACKAGE_DIR}" "${#found[@]}" >&2
    exit 1
  fi
  package_files+=("${found[0]}")
done

printf '\nPackages (relative to %s):\n' "${INVOCATION_DIR}"
for package_file in "${package_files[@]}"; do
  printf '  %s\n' "$(realpath --relative-to="${INVOCATION_DIR}" -- "${package_file}")"
done

if [[ "${install_package}" != true ]]; then
  exit 0
fi

native_package=""
for package_file in "${package_files[@]}"; do
  [[ "${package_file}" == *."${native_format}" ]] && native_package="${package_file}"
done

previous_version="$(installed_version)"
previous_files="$(installed_files)"

if [[ -n "${previous_version}" ]]; then
  printf '\nReplacing installed %s %s with %s...\n' "${PACKAGE_NAME}" "${previous_version}" \
    "$(basename -- "${native_package}")"
else
  printf '\nInstalling %s...\n' "$(basename -- "${native_package}")"
fi
install_package_file "${native_package}"

# The package manager deletes files the new package no longer contains. Check it did, and delete
# any that are left over unless another package now owns them.
current_files="$(installed_files)"
stale_files=()
if [[ -n "${previous_files}" ]]; then
  mapfile -t stale_files < <(LC_ALL=C comm -23 <(printf '%s\n' "${previous_files}") \
    <(printf '%s\n' "${current_files}"))
fi
for stale_file in "${stale_files[@]}"; do
  [[ -n "${stale_file}" ]] || continue
  if [[ -e "${stale_file}" || -L "${stale_file}" ]] && ! path_is_owned "${stale_file}"; then
    printf 'Removing left-over file of the old package: %s\n' "${stale_file}"
    as_root rm -f -- "${stale_file}"
  fi
done

printf '\n%s %s is installed:\n' "${PACKAGE_NAME}" "$(installed_version)"
printf '%s\n' "${current_files}" | sed 's/^/  /'
