#!/usr/bin/env bash
#
# Run example:
#   source ./deploy/docker/common.sh
# Command description:
#   Load shared Docker compose helpers for development-environment scripts.

set -euo pipefail

DOCKER_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${DOCKER_DIR}/../.." && pwd)
COMPOSE_FILE="${DOCKER_DIR}/docker-compose.yml"
ABE_REPO_ROOT=${ABE_REPO_ROOT:-${REPO_ROOT}}
export ABE_REPO_ROOT

quote_arg() {
  printf '%q' "$1"
}

env_prefix() {
  local name

  for name in "$@"; do
    if [ "${!name+x}" = x ]; then
      printf '%s=' "${name}"
      quote_arg "${!name}"
      printf ' '
    fi
  done
}

in_dev_container() {
  [ -f /.dockerenv ] && [ -d /workspace/server ]
}

require_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "docker command not found" >&2
    exit 127
  fi
}

docker_cmd() {
  if [ "$(id -u)" -eq 0 ] || docker info >/dev/null 2>&1; then
    docker "$@"
    return 0
  fi

  sudo docker "$@"
}

compose() {
  docker_cmd compose -f "${COMPOSE_FILE}" --project-directory "${DOCKER_DIR}" "$@"
}

ensure_compose_env() {
  if [ ! -f "${DOCKER_DIR}/.env" ] && [ -f "${DOCKER_DIR}/.env.example" ]; then
    cp "${DOCKER_DIR}/.env.example" "${DOCKER_DIR}/.env"
    echo "created ${DOCKER_DIR}/.env from .env.example"
  fi
}

ensure_dev_container() {
  require_docker
  ensure_compose_env
  compose up -d dev
}

ensure_dev_deps() {
  local check_cmd

  check_cmd='pkg-config --exists json-c libxml-2.0'
  if in_dev_container; then
    if ! sh -lc "${check_cmd}"; then
      echo "dev container is missing required build packages: json-c libxml2" >&2
      exit 1
    fi
    return 0
  fi

  ensure_dev_container
  if compose exec -T dev sh -lc "${check_cmd}"; then
    return 0
  fi

  echo "dev image is missing json-c/libxml2 development packages; rebuilding dev image"
  compose build dev
  compose up -d --force-recreate dev
  compose exec -T dev sh -lc "${check_cmd}"
}

run_in_dev_container() {
  local command

  command=$1
  if in_dev_container; then
    (cd "${REPO_ROOT}" && bash -lc "${command}")
    return 0
  fi

  ensure_dev_deps
  compose exec -T dev bash -lc "cd /workspace && ${command}"
}
