#!/usr/bin/env bash
#
# Run example:
#   ./deploy/docker/build.sh
# Command description:
#   Build project code inside the default Docker dev container.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "${SCRIPT_DIR}/common.sh"

usage() {
  cat <<'EOF'
Usage:
  deploy/docker/build.sh [target]

Defaults:
  target    abe_gateway
  BUILD_DIR /tmp/abe_engine_build/engine

Environment:
  BUILD_DIR   CMake build directory.
  JOBS        Parallel build jobs.
  CMAKE_ARGS  Extra arguments appended to the configure command.
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

BUILD_DIR=${BUILD_DIR:-/tmp/abe_engine_build/engine}
cmd="$(env_prefix BUILD_DIR JOBS CMAKE_ARGS)./scripts/build.sh"
if [ "$#" -eq 1 ]; then
  cmd="${cmd} $(quote_arg "$1")"
fi

run_in_dev_container "${cmd}"
