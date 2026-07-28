#!/usr/bin/env bash
#
# Run example:
#   ./scripts/services_start.sh gateway
# Command description:
#   Start one or more already-built project service processes in the current environment.
#
# Usage:
#   ./scripts/services_start.sh [service...]
#
# Services:
#   gateway    Start bin/abe_gateway with bin/gate.json.
#   all        Start all default services. Currently: gateway.
#
# Defaults:
#   service    gateway
#   RUN_DIR    bin/run
#   OUT_DIR    bin/logs
#
# Environment:
#   GATEWAY_BIN       Gateway binary path. Default: bin/abe_gateway
#   GATEWAY_CONFIG    Gateway config path. Default: bin/gate.json
#   GATEWAY_ARGS      Extra gateway arguments split by spaces.
#   GATEWAY_PID_FILE  Gateway pid file. Default: ${RUN_DIR}/gateway.pid
#   GATEWAY_OUT_FILE  Gateway stdout/stderr file. Default: ${OUT_DIR}/gateway/stdout.log
#   GATEWAY_LOG_DIR   Gateway daily log root. Default: ${OUT_DIR}/gateway
#
# Note:
#   This script does not build code, start Docker, or enter a container. Build
#   first with ./scripts/build.sh or ./deploy/docker/build.sh.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  scripts/services_start.sh [service...]

Services:
  gateway    Start bin/abe_gateway with bin/gate.json.
  all        Start all default services. Currently: gateway.

Defaults:
  service    gateway
  RUN_DIR    bin/run
  OUT_DIR    bin/logs

Environment:
  GATEWAY_BIN       Gateway binary path. Default: bin/abe_gateway
  GATEWAY_CONFIG    Gateway config path. Default: bin/gate.json
  GATEWAY_ARGS      Extra gateway arguments split by spaces.
  GATEWAY_PID_FILE  Gateway pid file. Default: ${RUN_DIR}/gateway.pid
  GATEWAY_OUT_FILE  Gateway stdout/stderr file. Default: ${OUT_DIR}/gateway/stdout.log
  GATEWAY_LOG_DIR   Gateway daily log root. Default: ${OUT_DIR}/gateway

This script does not start Docker or enter a container. The default project
runtime is the dev container /workspace; run this script there unless the host
already has the required runtime dependencies.
EOF
}

pid_namespace() {
  readlink /proc/self/ns/pid 2>/dev/null || echo unknown
}

process_state() {
  local pid

  pid=$1
  ps -o stat= -p "${pid}" 2>/dev/null | tr -d ' ' || true
}

print_status() {
  local service status pid

  service=$1
  status=$2
  pid=${3:--}

  printf '%-12s %-10s %s\n' "${service}" "${status}" "${pid}"
}

pid_file_pid() {
  local file line

  file=$1
  line=$(sed -n 's/^pid=//p' "${file}" 2>/dev/null | tail -n 1)
  if [ -n "${line}" ]; then
    printf '%s\n' "${line}"
    return 0
  fi

  line=$(head -n 1 "${file}" 2>/dev/null || true)
  case "${line}" in
    ''|*[!0-9]*)
      return 1
      ;;
    *)
      printf '%s\n' "${line}"
      return 0
      ;;
  esac
}

pid_file_value() {
  local key file

  key=$1
  file=$2
  sed -n "s/^${key}=//p" "${file}" 2>/dev/null | tail -n 1
}

is_running_pid_file() {
  local pid_file pid state file_ns current_ns

  pid_file=$1
  if [ ! -f "${pid_file}" ]; then
    return 1
  fi

  file_ns=$(pid_file_value pid_ns "${pid_file}")
  current_ns=$(pid_namespace)
  if [ -n "${file_ns}" ] && [ "${file_ns}" != "${current_ns}" ]; then
    return 1
  fi

  pid=$(pid_file_pid "${pid_file}" || true)
  if [ -z "${pid}" ]; then
    return 1
  fi

  state=$(process_state "${pid}")
  [ -n "${state}" ] && [ "${state#Z}" = "${state}" ]
}

write_pid_file() {
  local pid_file service pid

  pid_file=$1
  service=$2
  pid=$3

  {
    printf 'service=%s\n' "${service}"
    printf 'pid=%s\n' "${pid}"
    printf 'pid_ns=%s\n' "$(pid_namespace)"
  } >"${pid_file}"
}

start_gateway() {
  local binary config run_dir out_dir pid_file out_file gateway_args
  local pid state
  local -a extra_args

  binary=${GATEWAY_BIN:-bin/abe_gateway}
  config=${GATEWAY_CONFIG:-bin/gate.json}
  run_dir=${RUN_DIR:-bin/run}
  out_dir=${OUT_DIR:-bin/logs}
  pid_file=${GATEWAY_PID_FILE:-${run_dir}/gateway.pid}
  out_file=${GATEWAY_OUT_FILE:-${out_dir}/gateway/stdout.log}
  gateway_args=${GATEWAY_ARGS:-}

  mkdir -p "$(dirname "${pid_file}")" "$(dirname "${out_file}")"

  if [ ! -x "${binary}" ]; then
    echo "gateway binary not found: ${binary}" >&2
    echo "build it first with: ./scripts/build.sh abe_gateway" >&2
    exit 1
  fi

  if [ ! -f "${config}" ]; then
    echo "gateway config not found: ${config}" >&2
    exit 1
  fi

  if is_running_pid_file "${pid_file}"; then
    pid=$(pid_file_pid "${pid_file}")
    print_status gateway running "${pid}"
    return 0
  fi

  extra_args=()
  if [ -n "${gateway_args}" ]; then
    read -r -a extra_args <<< "${gateway_args}"
  fi

  nohup "${binary}" --config "${config}" "${extra_args[@]}" >"${out_file}" 2>&1 &
  pid=$!
  sleep 1

  state=$(process_state "${pid}")
  if [ -z "${state}" ] || [ "${state#Z}" != "${state}" ]; then
    rm -f "${pid_file}"
    echo "gateway failed to stay running; log=${out_file}" >&2
    tail -40 "${out_file}" >&2 2>/dev/null || true
    exit 1
  fi

  write_pid_file "${pid_file}" gateway "${pid}"
  print_status gateway started "${pid}"
}

start_service() {
  local service

  service=$1
  case "${service}" in
    gateway)
      start_gateway
      ;;
    *)
      echo "unknown service: ${service}" >&2
      exit 2
      ;;
  esac
}

expand_services() {
  local service

  if [ "$#" -eq 0 ]; then
    printf '%s\n' gateway
    return 0
  fi

  for service in "$@"; do
    case "${service}" in
      all)
        printf '%s\n' gateway
        ;;
      *)
        printf '%s\n' "${service}"
        ;;
    esac
  done
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

cd "${REPO_ROOT}"

while IFS= read -r service; do
  start_service "${service}"
done < <(expand_services "$@")
