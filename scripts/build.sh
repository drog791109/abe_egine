#!/usr/bin/env bash
#
# Run example:
#   ./scripts/build.sh
# Command description:
#   Configure CMake and build project code in the current environment.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  scripts/build.sh [target]

Defaults:
  target    abe_gateway
  BUILD_DIR build/engine

Environment:
  BUILD_DIR   CMake build directory.
  JOBS        Parallel build jobs.
  CMAKE_ARGS  Extra arguments appended to the configure command.

This script does not start Docker. Run it inside /workspace in the dev container,
or on a host that already has all build dependencies installed.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

cd "${REPO_ROOT}"

target=${1:-abe_gateway}
if [ "$#" -gt 1 ]; then
  usage >&2
  exit 2
fi

build_dir=${BUILD_DIR:-build/engine}
jobs=${JOBS:-}
if [ -z "${jobs}" ]; then
  jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
fi

configure_args=(-S server/engine -B "${build_dir}" -DABE_ENGINE_BUILD_TESTS=ON)
if [ -n "${CMAKE_ARGS:-}" ]; then
  read -r -a extra_cmake_args <<< "${CMAKE_ARGS}"
  configure_args+=("${extra_cmake_args[@]}")
fi

cmake "${configure_args[@]}"

build_args=(--build "${build_dir}")
if [ "${target}" != "all" ]; then
  build_args+=(--target "${target}")
fi
build_args+=(-j "${jobs}")

cmake "${build_args[@]}"
