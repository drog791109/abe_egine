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
  target    abe_gateway
  BUILD_DIR build/engine

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

build_dir=${BUILD_DIR:-build/engine}

case "${build_dir}" in
  ""|"/"|".")
    echo "unsafe BUILD_DIR: ${build_dir}" >&2
    exit 2
    ;;
esac

cd "${REPO_ROOT}"
rm -rf "${build_dir}"
exec "${SCRIPT_DIR}/build.sh" "$@"
