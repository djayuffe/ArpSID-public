#!/usr/bin/env bash
# ArpSID source-root build helper.
#
# Default behavior is intentionally safe and source-only:
#   ./build.sh
# configures CMake, builds, and runs CTest in ./build.
#
# macOS AUv2 install is opt-in only:
#   ./build.sh --install-auv2
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORIGINAL_ARGS=("$@")
BUILD_DIR="${ARPSID_BUILD_DIR:-${ROOT}/build}"
CONFIG="${ARPSID_BUILD_CONFIG:-Release}"
RUN_TESTS=1
INSTALL_AUV2=0
CLEAR_AU_CACHE=0
RELEASE_CHECK=0
PACKAGE_RELEASE=0
VALIDATE_AUV2=0
AUVAL_STRICT=0
MACOS_CLOSURE=0
CLOSURE_LOG_DIR="${ARPSID_CLOSURE_LOG_DIR:-}"
TARGET=""
TEST_FILTER=""
PARALLEL="${ARPSID_BUILD_JOBS:-}"
GENERATOR="${ARPSID_CMAKE_GENERATOR:-}"

usage() {
  cat <<USAGE
ArpSID build helper

Usage:
  ./build.sh [options]

Options:
  --build-dir DIR       Build directory (default: ./build or ARPSID_BUILD_DIR)
  --config NAME         Build config (default: Release or ARPSID_BUILD_CONFIG)
  --generator NAME      CMake generator (default: Ninja if available, else CMake default)
  --target NAME         Build a specific CMake target
  --test-filter REGEX   Run only matching CTest tests
  --no-tests            Configure/build only; skip CTest
  --parallel N          Parallel build jobs passed to cmake --build
  --install-auv2        macOS only: install built ArpSID.component after build
  --clear-au-cache      macOS only: clear AudioComponent registrar cache after install
  --release-check       Build and run the curated release-closure contract suite
  --package-release     Run release-check, then create a clean source release zip
  --validate-auv2       macOS only: run strict auval for installed AUv2 flavors
  --strict-auval        Alias for --validate-auv2
  --macos-closure       macOS only: release-check + full CTest + install + cache refresh + auval
  --closure-log-dir DIR  Capture macOS closure/auval output under DIR (default: ./build/release-logs)
  -h, --help            Show this help

Examples:
  ./build.sh
  ./build.sh --build-dir build-release --parallel 8
  ./build.sh --test-filter 'Auv2Version|SourceOnlyClosure'
  ./build.sh --install-auv2 --clear-au-cache
  ./build.sh --release-check
  ./build.sh --package-release
  ./build.sh --install-auv2 --clear-au-cache --validate-auv2
  ./build.sh --macos-closure
  ./build.sh --macos-closure --closure-log-dir ./release-logs
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --build-dir)
      [ "$#" -ge 2 ] || { echo "--build-dir requires a value" >&2; exit 2; }
      BUILD_DIR="$2"; shift 2 ;;
    --config)
      [ "$#" -ge 2 ] || { echo "--config requires a value" >&2; exit 2; }
      CONFIG="$2"; shift 2 ;;
    --generator)
      [ "$#" -ge 2 ] || { echo "--generator requires a value" >&2; exit 2; }
      GENERATOR="$2"; shift 2 ;;
    --target)
      [ "$#" -ge 2 ] || { echo "--target requires a value" >&2; exit 2; }
      TARGET="$2"; shift 2 ;;
    --test-filter)
      [ "$#" -ge 2 ] || { echo "--test-filter requires a value" >&2; exit 2; }
      TEST_FILTER="$2"; shift 2 ;;
    --no-tests)
      RUN_TESTS=0; shift ;;
    --parallel)
      [ "$#" -ge 2 ] || { echo "--parallel requires a value" >&2; exit 2; }
      PARALLEL="$2"; shift 2 ;;
    --install-auv2)
      INSTALL_AUV2=1; shift ;;
    --clear-au-cache)
      CLEAR_AU_CACHE=1; shift ;;
    --release-check)
      RELEASE_CHECK=1; shift ;;
    --package-release)
      PACKAGE_RELEASE=1; RELEASE_CHECK=1; shift ;;
    --validate-auv2)
      VALIDATE_AUV2=1; AUVAL_STRICT=1; shift ;;
    --strict-auval)
      VALIDATE_AUV2=1; AUVAL_STRICT=1; shift ;;
    --macos-closure)
      MACOS_CLOSURE=1; RELEASE_CHECK=1; RUN_TESTS=1; INSTALL_AUV2=1; CLEAR_AU_CACHE=1; VALIDATE_AUV2=1; AUVAL_STRICT=1; shift ;;
    --closure-log-dir)
      [ "$#" -ge 2 ] || { echo "--closure-log-dir requires a value" >&2; exit 2; }
      CLOSURE_LOG_DIR="$2"; shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2 ;;
  esac
done

command -v cmake >/dev/null 2>&1 || { echo "cmake not found in PATH" >&2; exit 127; }

if [ "$MACOS_CLOSURE" -eq 1 ] && [ "$(uname -s)" != "Darwin" ]; then
  echo "--macos-closure is macOS-only" >&2
  exit 9
fi

if [ -n "$CLOSURE_LOG_DIR" ] || [ "$MACOS_CLOSURE" -eq 1 ]; then
  if [ -z "$CLOSURE_LOG_DIR" ]; then
    CLOSURE_LOG_DIR="$BUILD_DIR/release-logs"
  fi
  mkdir -p "$CLOSURE_LOG_DIR"
  LOG_FILE="$CLOSURE_LOG_DIR/macos-closure-$(date +%Y%m%d-%H%M%S).log"
  echo "[ArpSID] capture log: $LOG_FILE"
  exec > >(tee -a "$LOG_FILE") 2>&1
  echo "[ArpSID] log started: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  ORIGINAL_COMMAND="$0"
  for arg in "${ORIGINAL_ARGS[@]}"; do
    printf -v ARPSID_QUOTED_ARG '%q' "$arg"
    ORIGINAL_COMMAND+=" ${ARPSID_QUOTED_ARG}"
  done
  echo "[ArpSID] command: ${ORIGINAL_COMMAND}"
fi

CMAKE_CONFIGURE_ARGS=(-S "$ROOT" -B "$BUILD_DIR")
if [ -n "$GENERATOR" ]; then
  CMAKE_CONFIGURE_ARGS+=(-G "$GENERATOR")
elif command -v ninja >/dev/null 2>&1; then
  CMAKE_CONFIGURE_ARGS+=(-G Ninja)
fi
CMAKE_CONFIGURE_ARGS+=(-DCMAKE_BUILD_TYPE="$CONFIG")
if [ "$INSTALL_AUV2" -eq 1 ] || [ "$VALIDATE_AUV2" -eq 1 ]; then
  CMAKE_CONFIGURE_ARGS+=(-DARPSID_BUILD_AUV2=ON)
fi
if [ "$RUN_TESTS" -eq 1 ]; then
  CMAKE_CONFIGURE_ARGS+=(-DARPSID_BUILD_TESTS=ON)
else
  CMAKE_CONFIGURE_ARGS+=(-DARPSID_BUILD_TESTS=OFF)
fi

if [ "$MACOS_CLOSURE" -eq 1 ]; then
  echo "[ArpSID] macOS closure: release-check + full CTest + AUv2 install/cache refresh + strict auval"
fi

echo "[ArpSID] configure: ${CMAKE_CONFIGURE_ARGS[*]}"
cmake "${CMAKE_CONFIGURE_ARGS[@]}"

BUILD_ARGS=(--build "$BUILD_DIR" --config "$CONFIG")
if [ -n "$TARGET" ]; then
  BUILD_ARGS+=(--target "$TARGET")
fi
if [ -n "$PARALLEL" ]; then
  BUILD_ARGS+=(--parallel "$PARALLEL")
fi

echo "[ArpSID] build: cmake ${BUILD_ARGS[*]}"
cmake "${BUILD_ARGS[@]}"

if [ "$RELEASE_CHECK" -eq 1 ]; then
  echo "[ArpSID] release-check: build arpsid_release_closure_suite"
  cmake --build "$BUILD_DIR" --config "$CONFIG" --target arpsid_release_closure_suite
  echo "[ArpSID] release-check: ctest curated closure guards"
  ctest --test-dir "$BUILD_DIR" --build-config "$CONFIG" -R "ReleaseClosureV701Tests|ReleasePackagingV702Tests|TabNoScaffoldV631Tests|TabTooltipCoverageV586Tests|BankUITextAndTooltipV677Tests|NoDead127BankSlotDecodeV694Tests|FactoryPayloadFinalDeepGuardV695Tests" --output-on-failure
fi

if [ "$RUN_TESTS" -eq 1 ]; then
  CTEST_ARGS=(--test-dir "$BUILD_DIR" --build-config "$CONFIG" --output-on-failure)
  if [ -n "$TEST_FILTER" ]; then
    CTEST_ARGS+=(-R "$TEST_FILTER")
  fi
  echo "[ArpSID] test: ctest ${CTEST_ARGS[*]}"
  ctest "${CTEST_ARGS[@]}"
fi

if [ "$INSTALL_AUV2" -eq 1 ]; then
  if [ "$(uname -s)" != "Darwin" ]; then
    echo "--install-auv2 is macOS-only" >&2
    exit 3
  fi
  INSTALL_SCRIPT="$ROOT/scripts/macos/install_auv2_component.sh"
  [ -x "$INSTALL_SCRIPT" ] || { echo "missing or non-executable: $INSTALL_SCRIPT" >&2; exit 4; }
  echo "[ArpSID] build AUv2 bundle target before install"
  cmake --build "$BUILD_DIR" --config "$CONFIG" --target arpsid_auv2
  if ! find "$BUILD_DIR" -name ArpSID.component -type d | grep -q .; then
    echo "ArpSID.component not found under ${BUILD_DIR} after building arpsid_auv2" >&2
    exit 5
  fi
  USER_COMPONENT="$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
  SYSTEM_COMPONENT="/Library/Audio/Plug-Ins/Components/ArpSID.component"
  if [ -d "$USER_COMPONENT" ]; then
    echo "[ArpSID] remove stale user AUv2 component: $USER_COMPONENT"
    rm -rf "$USER_COMPONENT"
  fi
  if [ -d "$SYSTEM_COMPONENT" ]; then
    echo "System AUv2 duplicate exists: $SYSTEM_COMPONENT" >&2
    echo "Remove it with scripts/macos/uninstall_auv2_component.sh --system, then rerun install." >&2
    exit 5
  fi
  echo "[ArpSID] install AUv2 component from $BUILD_DIR"
  "$INSTALL_SCRIPT" "$BUILD_DIR" "$HOME/Library/Audio/Plug-Ins/Components" -
  if [ "$CLEAR_AU_CACHE" -eq 1 ]; then
    REFRESH_SCRIPT="$ROOT/scripts/macos/refresh_auv2_component.sh"
    if [ -x "$REFRESH_SCRIPT" ]; then
      "$REFRESH_SCRIPT" "$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
    else
      killall -9 AudioComponentRegistrar 2>/dev/null || true
    fi
  fi
fi

if [ "$VALIDATE_AUV2" -eq 1 ]; then
  if [ "$(uname -s)" != "Darwin" ]; then
    echo "--validate-auv2 is macOS-only" >&2
    exit 6
  fi
  VERIFY_SCRIPT="$ROOT/scripts/macos/verify_auv2_component.sh"
  [ -x "$VERIFY_SCRIPT" ] || { echo "missing or non-executable: $VERIFY_SCRIPT" >&2; exit 7; }
  COMPONENT_PATH="$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
  if [ ! -d "$COMPONENT_PATH" ]; then
    echo "Installed AUv2 component not found: $COMPONENT_PATH" >&2
    echo "Run ./build.sh --install-auv2 --clear-au-cache first, or combine it with --validate-auv2." >&2
    exit 8
  fi
  echo "[ArpSID] validate AUv2 component with strict auval: $COMPONENT_PATH"
  "$VERIFY_SCRIPT" "$COMPONENT_PATH"
fi

if [ "$PACKAGE_RELEASE" -eq 1 ]; then
  echo "[ArpSID] package-release: scripts/package_release.sh"
  "$ROOT/scripts/package_release.sh" "$ROOT"
fi

echo "[ArpSID] done"
