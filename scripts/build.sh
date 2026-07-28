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
  target      abe_gateway
  BUILD_DIR   build/engine, or /tmp/abe_engine_build_<id> on VMware shared mounts

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

repo_fs_type() {
  df -T "${REPO_ROOT}" 2>/dev/null | awk 'NR == 2 { print $2 }'
}

default_build_dir() {
  local fs_type repo_key

  fs_type=$(repo_fs_type || true)
  case "${fs_type}" in
    vmhgfs-fuse|fuse.vmhgfs-fuse)
      repo_key=$(printf '%s' "${REPO_ROOT}" | cksum | awk '{ print $1 }')
      printf '/tmp/abe_engine_build_%s\n' "${repo_key}"
      ;;
    *)
      printf 'build/engine\n'
      ;;
  esac
}

target=${1:-abe_gateway}
if [ "$#" -gt 1 ]; then
  usage >&2
  exit 2
fi

build_dir=${BUILD_DIR:-$(default_build_dir)}
if [ -z "${BUILD_DIR:-}" ]; then
  case "$(repo_fs_type || true)" in
    vmhgfs-fuse|fuse.vmhgfs-fuse)
      echo "using container-local BUILD_DIR=${build_dir} for VMware shared workspace"
      ;;
  esac
fi

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
