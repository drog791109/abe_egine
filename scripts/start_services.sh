#!/usr/bin/env bash
#
# Run example:
#   ./scripts/services_start.sh login gatehub gateway
# Command description:
#   Start one or more already-built project service processes in the current environment.
#
# Usage:
#   ./scripts/services_start.sh [service...]
#
# Services:
#   gateway    Start server/bin/abe_gateway with server/bin/gate.json.
#   login      Start server/bin/abe_login with server/bin/login.json.
#   gatehub    Start server/bin/abe_gatehub with server/bin/gatehub.json.
#   all        Start all default services. Currently: login, gatehub, gateway.
#
# Defaults:
#   service    login gatehub gateway
#   RUN_DIR    server/bin/run
#   OUT_DIR    server/bin/logs
#
# Environment:
#   GATEWAY_BIN       Gateway binary path. Default: server/bin/abe_gateway
#   GATEWAY_PID_FILE  Gateway pid file. Default: ${RUN_DIR}/gateway.pid
#   GATEWAY_OUT_FILE  Gateway stdout/stderr file. Default: ${OUT_DIR}/gateway/stdout.log
#   GATEWAY_LOG_DIR   Gateway daily log root. Default: ${OUT_DIR}/gateway
#   LOGIN_BIN         Login binary path. Default: server/bin/abe_login
#   GATEHUB_BIN       Gatehub binary path. Default: server/bin/abe_gatehub
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
  gateway    Start server/bin/abe_gateway with server/bin/gate.json.
  login      Start server/bin/abe_login with server/bin/login.json.
  gatehub    Start server/bin/abe_gatehub with server/bin/gatehub.json.
  all        Start all default services. Currently: login, gatehub, gateway.

Defaults:
  service    login gatehub gateway
  RUN_DIR    server/bin/run
  OUT_DIR    server/bin/logs

Environment:
  GATEWAY_BIN       Gateway binary path. Default: server/bin/abe_gateway
  GATEWAY_PID_FILE  Gateway pid file. Default: ${RUN_DIR}/gateway.pid
  GATEWAY_OUT_FILE  Gateway stdout/stderr file. Default: ${OUT_DIR}/gateway/stdout.log
  GATEWAY_LOG_DIR   Gateway daily log root. Default: ${OUT_DIR}/gateway
  LOGIN_BIN         Login binary path. Default: server/bin/abe_login
  GATEHUB_BIN       Gatehub binary path. Default: server/bin/abe_gatehub

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

start_runtime_service() {
  local service service_key binary_var pid_file_var out_file_var
  local default_config binary config run_dir out_dir pid_file out_file
  local pid state

  service=$1
  service_key=$(printf '%s' "${service}" | tr '[:lower:]' '[:upper:]')
  binary_var=${service_key}_BIN
  pid_file_var=${service_key}_PID_FILE
  out_file_var=${service_key}_OUT_FILE

  default_config=server/bin/${service}.json
  if [ "${service}" = "gateway" ]; then
    default_config=server/bin/gate.json
  fi

  binary=${!binary_var:-server/bin/abe_${service}}
  config=${default_config}
  run_dir=${RUN_DIR:-server/bin/run}
  out_dir=${OUT_DIR:-server/bin/logs}
  pid_file=${!pid_file_var:-${run_dir}/${service}.pid}
  out_file=${!out_file_var:-${out_dir}/${service}/stdout.log}

  mkdir -p "$(dirname "${pid_file}")" "$(dirname "${out_file}")"

  if [ ! -x "${binary}" ]; then
    echo "${service} binary not found: ${binary}" >&2
    echo "build it first with: ./scripts/build.sh abe_${service}" >&2
    exit 1
  fi

  if [ ! -f "${config}" ]; then
    echo "${service} config not found: ${config}" >&2
    exit 1
  fi

  if is_running_pid_file "${pid_file}"; then
    pid=$(pid_file_pid "${pid_file}")
    print_status "${service}" running "${pid}"
    return 0
  fi

  nohup "${binary}" >"${out_file}" 2>&1 &
  pid=$!
  sleep 1

  state=$(process_state "${pid}")
  if [ -z "${state}" ] || [ "${state#Z}" != "${state}" ]; then
    rm -f "${pid_file}"
    echo "${service} failed to stay running; log=${out_file}" >&2
    tail -40 "${out_file}" >&2 2>/dev/null || true
    exit 1
  fi

  write_pid_file "${pid_file}" "${service}" "${pid}"
  print_status "${service}" started "${pid}"
}

start_service() {
  local service

  service=$1
  case "${service}" in
    gateway|login|gatehub)
      start_runtime_service "${service}"
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
    printf '%s\n' login gatehub gateway
    return 0
  fi

  for service in "$@"; do
    case "${service}" in
      all)
        printf '%s\n' login gatehub gateway
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
