#!/usr/bin/env bash
#
# Run example:
#   ./scripts/rebuild.sh
# Command description:
#   Remove the CMake build directory, then rebuild project code in the current environment.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  scripts/rebuild.sh [target]

Defaults:
  target      all
  BUILD_DIR   build/engine, or /tmp/abe_engine_build_<id> on VMware shared mounts

This script does not start Docker. Run it inside /workspace in the dev container,
or on a host that already has all build dependencies installed.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ "$#" -gt 1 ]; then
  usage >&2
  exit 2
fi

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

build_dir=${BUILD_DIR:-$(default_build_dir)}

case "${build_dir}" in
  ""|"/"|".")
    echo "unsafe BUILD_DIR: ${build_dir}" >&2
    exit 2
    ;;
esac

cd "${REPO_ROOT}"
rm -rf "${build_dir}"
exec "${SCRIPT_DIR}/build.sh" "$@"
