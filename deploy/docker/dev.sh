#!/usr/bin/env bash
#
# Run example:
#   ./deploy/docker/dev.sh start
# Command description:
#   Build changed images and start the Docker development environment.
#
# ABE Engine Docker development helper.
#
# Usage:
#   ./dev.sh <command> [args...]
#
# Common commands:
#   ./dev.sh start                 Build changed images and start all services in background.
#   ./dev.sh stop                  Stop and remove containers, keep volumes.
#   ./dev.sh restart               Restart the environment and rebuild changed images.
#   ./dev.sh build [SERVICE...]    Build compose images.
#   ./dev.sh rebuild [SERVICE...]  Rebuild without cache and start services.
#   ./dev.sh status                Show service status.
#   ./dev.sh logs [SERVICE...]     Follow logs, optionally for selected services.
#   ./dev.sh enter                 Enter the dev container with bash.
#   ./dev.sh clean                 Stop and remove containers and volumes.
#   ./dev.sh config                Render compose config.
#   ./dev.sh portable              Build and export abe-engine-portable.tar.
#   ./dev.sh access [USER]         Permanently add USER to the docker group.
#
# Docker mirror commands:
#   ./dev.sh mirror-aliyun "$ALIYUN_MIRROR_URL"
#   ./dev.sh mirror-show
#
# Optional environment:
#   IMAGE_NAME=abe-engine-portable:latest
#   OUTPUT_FILE=/path/to/abe-engine-portable.tar
#   ALIYUN_MIRROR_URL=<copy the full Aliyun mirror URL from the ACR console>
#   ABE_REPO_ROOT=/path/to/abe_engine
#   BUILD_LIBJUICE=1
#
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"
ABE_REPO_ROOT=${REPO_ROOT}
export ABE_REPO_ROOT

usage() {
  cat <<'EOF'
Usage:
  dev.sh <command> [args...]

Commands:
  start [SERVICE...]        Build changed images and start services in background.
  stop [ARGS...]            Stop and remove containers, keep volumes.
  restart [SERVICE...]      Restart the environment, rebuilding changed images.
  build [SERVICE...]        Build compose images.
  rebuild [SERVICE...]      Rebuild without cache and start services.
  status [ARGS...]          Show compose service status.
  logs [SERVICE...]         Follow logs.
  enter [ARGS...]           Enter the dev container with bash.
  shell [ARGS...]           Alias of enter.
  clean [ARGS...]           Stop and remove containers and volumes.
  config [ARGS...]          Render compose config.
  portable                  Build and export the portable image tar.
  access [USER]             Permanently add USER, or the current login user, to the docker group.
  mirror-aliyun [URL]       Configure Docker daemon to use an Aliyun mirror.
  mirror-show               Show current Docker registry mirrors.
  help                      Show this help.

Environment:
  IMAGE_NAME                Portable image name. Default: abe-engine-portable:latest
  OUTPUT_FILE               Portable tar path. Default: <repo>/abe-engine-portable.tar
  ALIYUN_MIRROR_URL         Full Aliyun mirror URL copied from the ACR console.
  ABE_REPO_ROOT             Host source path mounted to /workspace. Default: <repo>
  BUILD_LIBJUICE            Build libjuice from source in the dev image. Default: 1
EOF
}

require_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "docker command not found" >&2
    exit 127
  fi
}

compose() {
  docker compose -f "${COMPOSE_FILE}" --project-directory "${SCRIPT_DIR}" "$@"
}

compose_privileged() {
  run_docker compose -f "${COMPOSE_FILE}" --project-directory "${SCRIPT_DIR}" "$@"
}

ensure_env() {
  if [ ! -f "${SCRIPT_DIR}/.env" ] && [ -f "${SCRIPT_DIR}/.env.example" ]; then
    cp "${SCRIPT_DIR}/.env.example" "${SCRIPT_DIR}/.env"
    echo "created ${SCRIPT_DIR}/.env from .env.example"
  fi
}

env_file_value() {
  local key file value

  key=$1
  shift

  for file in "${SCRIPT_DIR}/.env" "${SCRIPT_DIR}/.env.example"; do
    if [ -f "${file}" ]; then
      value=$(grep -E "^[[:space:]]*${key}=" "${file}" | tail -n 1 | cut -d= -f2- || true)
      if [ -n "${value}" ]; then
        printf '%s\n' "${value}"
        return 0
      fi
    fi
  done

  return 1
}

build_portable() {
  local image_name output_file base_image build_libjuice libjuice_ref tz

  image_name=${IMAGE_NAME:-abe-engine-portable:latest}
  output_file=${OUTPUT_FILE:-${REPO_ROOT}/abe-engine-portable.tar}
  base_image=${BASE_IMAGE:-}
  build_libjuice=${BUILD_LIBJUICE:-}
  libjuice_ref=${LIBJUICE_REF:-}
  tz=${TZ:-UTC}

  if [ -z "${base_image}" ]; then
    base_image=$(env_file_value BASE_IMAGE || true)
  fi
  if [ -z "${base_image}" ]; then
    base_image=public.ecr.aws/ubuntu/ubuntu:22.04
  fi
  if [ -z "${build_libjuice}" ]; then
    build_libjuice=$(env_file_value BUILD_LIBJUICE || true)
  fi
  if [ -z "${build_libjuice}" ]; then
    build_libjuice=1
  fi
  if [ -z "${libjuice_ref}" ]; then
    libjuice_ref=$(env_file_value LIBJUICE_REF || true)
  fi

  run_docker build \
    -f "${SCRIPT_DIR}/Dockerfile" \
    --target portable \
    --build-arg BASE_IMAGE="${base_image}" \
    --build-arg TZ="${tz}" \
    --build-arg BUILD_LIBJUICE="${build_libjuice}" \
    --build-arg LIBJUICE_REF="${libjuice_ref}" \
    -t "${image_name}" \
    "${REPO_ROOT}"

  run_docker save "${image_name}" -o "${output_file}"
  echo "saved ${image_name} to ${output_file}"
}

run_docker() {
  if [ "$(id -u)" -eq 0 ] || docker info >/dev/null 2>&1; then
    docker "$@"
    return $?
  fi

  sudo docker "$@"
}

run_privileged() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
    return $?
  fi

  sudo "$@"
}

current_login_user() {
  if [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER}" != "root" ]; then
    printf '%s\n' "${SUDO_USER}"
    return 0
  fi
  if [ -n "${USER:-}" ]; then
    printf '%s\n' "${USER}"
    return 0
  fi
  id -un
}

configure_docker_access() {
  local target_user socket_group

  target_user=${1:-$(current_login_user)}
  if [ -z "${target_user}" ]; then
    echo "target user is required" >&2
    exit 2
  fi

  if ! id "${target_user}" >/dev/null 2>&1; then
    echo "user not found: ${target_user}" >&2
    exit 2
  fi

  if [ "${target_user}" = "root" ]; then
    echo "root can already access Docker; pass a normal user if you want to grant Docker access" >&2
    exit 2
  fi

  if ! getent group docker >/dev/null 2>&1; then
    run_privileged groupadd docker
    echo "created docker group"
  fi

  if id -nG "${target_user}" | tr ' ' '\n' | grep -qx docker; then
    echo "${target_user} is already in the docker group"
  else
    run_privileged usermod -aG docker "${target_user}"
    echo "added ${target_user} to the docker group"
  fi

  if [ -S /var/run/docker.sock ]; then
    socket_group=$(stat -c '%G' /var/run/docker.sock 2>/dev/null || true)
    if [ "${socket_group}" != "docker" ]; then
      echo "warning: /var/run/docker.sock group is ${socket_group:-unknown}, expected docker" >&2
      echo "restart Docker or check docker.socket SocketGroup if docker access still fails" >&2
    fi
  fi

  cat <<EOF
Docker access has been configured permanently for ${target_user}.

Open a new login session, or run this in the current terminal for immediate effect:
  newgrp docker

Then verify with:
  docker ps
EOF
}

configure_aliyun_mirror() {
  local mirror_url current_json next_json backup_path

  mirror_url=${1:-${ALIYUN_MIRROR_URL:-}}
  if [ -z "${mirror_url}" ]; then
    mirror_url=$(env_file_value ALIYUN_MIRROR_URL || true)
  fi
  if [ -z "${mirror_url}" ]; then
    cat >&2 <<'EOF'
Aliyun mirror URL is required.

Get it from Alibaba Cloud Container Registry:
  https://cr.console.aliyun.com/cn-hangzhou/instances/mirrors

Usage:
  deploy/docker/dev.sh mirror-aliyun <full Aliyun mirror URL copied from the console>

Or:
  ALIYUN_MIRROR_URL=<full Aliyun mirror URL> deploy/docker/dev.sh mirror-aliyun
EOF
    exit 2
  fi

  case "${mirror_url}" in
    *xxxx.mirror.aliyuncs.com*)
      echo "mirror URL still contains placeholder text; copy the full URL from the Alibaba Cloud ACR console" >&2
      exit 2
      ;;
    http://*|https://*) ;;
    *)
      echo "mirror URL must start with http:// or https://: ${mirror_url}" >&2
      exit 2
      ;;
  esac

  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 command not found; cannot safely update /etc/docker/daemon.json" >&2
    exit 127
  fi

  current_json=$(mktemp)
  next_json=$(mktemp)
  trap 'rm -f "${current_json}" "${next_json}"' RETURN

  if run_privileged test -f /etc/docker/daemon.json; then
    run_privileged cat /etc/docker/daemon.json > "${current_json}"
    backup_path="/etc/docker/daemon.json.bak.$(date +%Y%m%d%H%M%S)"
    run_privileged cp /etc/docker/daemon.json "${backup_path}"
    echo "backed up /etc/docker/daemon.json to ${backup_path}"
  else
    printf '{}\n' > "${current_json}"
  fi

  python3 - "${current_json}" "${mirror_url}" > "${next_json}" <<'PY'
import json
import sys

path = sys.argv[1]
mirror = sys.argv[2]

try:
    with open(path, "r", encoding="utf-8") as f:
        raw = f.read().strip()
    data = json.loads(raw) if raw else {}
except json.JSONDecodeError as exc:
    print(f"invalid /etc/docker/daemon.json: {exc}", file=sys.stderr)
    sys.exit(2)

if not isinstance(data, dict):
    print("/etc/docker/daemon.json must contain a JSON object", file=sys.stderr)
    sys.exit(2)

mirrors = data.get("registry-mirrors", [])
if not isinstance(mirrors, list):
    mirrors = []

mirrors = [m for m in mirrors if m != mirror]
data["registry-mirrors"] = [mirror] + mirrors

json.dump(data, sys.stdout, ensure_ascii=False, indent=2)
sys.stdout.write("\n")
PY

  run_privileged install -m 0644 "${next_json}" /etc/docker/daemon.json
  run_privileged systemctl daemon-reload
  run_privileged systemctl restart docker
  echo "configured Docker registry mirror: ${mirror_url}"
  run_docker info --format 'registry mirrors: {{json .RegistryConfig.Mirrors}}' || true
}

show_mirrors() {
  require_docker
  run_docker info --format 'registry mirrors: {{json .RegistryConfig.Mirrors}}'
}

main() {
  local command

  command=${1:-help}
  if [ "$#" -gt 0 ]; then
    shift
  fi

  case "${command}" in
    help|-h|--help)
      usage
      ;;
    start|up)
      require_docker
      ensure_env
      compose_privileged up -d --build "$@"
      compose_privileged ps
      ;;
    stop|down)
      require_docker
      compose_privileged down "$@"
      ;;
    restart)
      require_docker
      ensure_env
      compose_privileged down
      compose_privileged up -d --build "$@"
      compose_privileged ps
      ;;
    build)
      require_docker
      ensure_env
      compose_privileged build "$@"
      ;;
    rebuild)
      require_docker
      ensure_env
      compose_privileged build --no-cache "$@"
      compose_privileged up -d --force-recreate "$@"
      compose_privileged ps
      ;;
    status|ps)
      require_docker
      compose_privileged ps "$@"
      ;;
    logs)
      require_docker
      compose_privileged logs -f "$@"
      ;;
    enter|shell|bash)
      require_docker
      ensure_env
      compose_privileged exec dev bash "$@"
      ;;
    clean)
      require_docker
      compose_privileged down -v "$@"
      ;;
    config)
      require_docker
      ensure_env
      compose config "$@"
      ;;
    portable|build-portable)
      require_docker
      build_portable
      ;;
    access|docker-access|docker-permission|docker-permissions)
      configure_docker_access "$@"
      ;;
    mirror-aliyun|aliyun-mirror)
      require_docker
      configure_aliyun_mirror "$@"
      ;;
    mirror-show|mirrors)
      show_mirrors
      ;;
    *)
      echo "unknown command: ${command}" >&2
      usage >&2
      exit 2
      ;;
  esac
}

main "$@"
