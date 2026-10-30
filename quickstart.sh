#!/usr/bin/env bash

set -Eeuo pipefail

readonly INVOCATION_DIR="$(pwd -P)"
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly PRESET="dev"
readonly BUILD_DIR="${SCRIPT_DIR}/build/${PRESET}"
readonly INSTALL_DIR="${BUILD_DIR}/install"

install_dependencies=true
run_tests=true

usage() {
  cat <<'EOF'
Usage: ./quickstart.sh [options]

Configure, build, test, and locally install gb-cplusplus.

Options:
  --no-install-deps  Do not try to install missing Ubuntu packages
  --skip-tests       Build without running the test suite
  -h, --help         Show this help and exit

Missing build tools are installed with apt-get when it is available. The script
uses sudo when needed and may prompt for the user's password. Build products stay
inside build/dev; no project executable is installed system-wide.
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

missing_packages=()
command -v c++ >/dev/null 2>&1 || missing_packages+=(build-essential)
command -v cmake >/dev/null 2>&1 || missing_packages+=(cmake)
command -v ninja >/dev/null 2>&1 || missing_packages+=(ninja-build)
command -v pkg-config >/dev/null 2>&1 || missing_packages+=(pkg-config)
command -v ffmpeg >/dev/null 2>&1 || missing_packages+=(ffmpeg)
if ! command -v pkg-config >/dev/null 2>&1 ||
   ! pkg-config --exists libavformat libavcodec libavutil libswscale; then
  missing_packages+=(libavformat-dev libavcodec-dev libavutil-dev libswscale-dev)
fi
if ! pkg-config --exists sdl2 2>/dev/null; then
  missing_packages+=(libsdl2-dev)
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
    printf 'Install a C++20 compiler, CMake 3.22+, Ninja 1.10+, pkg-config, FFmpeg development libraries, and Qt 6 development/QML packages, then rerun.\n' >&2
    exit 1
  fi

  apt_command=(apt-get)
  if ((EUID != 0)); then
    if ! command -v sudo >/dev/null 2>&1; then
      printf 'sudo is required to install missing packages.\n' >&2
      exit 1
    fi
    apt_command=(sudo apt-get)
  fi

  printf 'Installing missing build dependencies with apt-get...\n'
  "${apt_command[@]}" update
  "${apt_command[@]}" install -y --no-install-recommends "${missing_packages[@]}"
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
