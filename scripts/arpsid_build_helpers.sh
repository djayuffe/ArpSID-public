#!/usr/bin/env bash
# Shared build helpers for ArpSID validation scripts.
# shellcheck shell=bash

arpsid_ncpu() {
  getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4
}

arpsid_cache_source_dir() {
  local cache="$1/CMakeCache.txt"
  [[ -f "${cache}" ]] || return 1
  sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache}" | head -n 1
}

arpsid_ensure_fresh_build_dir() {
  local root="$1"
  local build="$2"
  local clean="${3:-0}"

  if [[ "${clean}" == "1" || "${clean}" == "true" || "${clean}" == "yes" ]]; then
    echo "== Removing build dir because clean build was requested: ${build} =="
    rm -rf "${build}"
    return 0
  fi

  if [[ -f "${build}/CMakeCache.txt" ]]; then
    local cached
    cached="$(arpsid_cache_source_dir "${build}" || true)"
    if [[ -n "${cached}" && "${cached}" != "${root}" ]]; then
      echo "== Removing stale CMake build dir =="
      echo "BUILD:        ${build}"
      echo "Cached root:  ${cached}"
      echo "Current root: ${root}"
      rm -rf "${build}"
    fi
  fi
}

arpsid_build_with_serial_retry() {
  local build="$1"
  local jobs="$2"
  shift 2
  set +e
  cmake --build "${build}" -j"${jobs}" "$@"
  local rc=$?
  set -e
  if [[ ${rc} -ne 0 ]]; then
    echo "WARN: parallel build failed; retrying serially for first deterministic compiler error" >&2
    cmake --build "${build}" -j1 "$@"
  fi
}
