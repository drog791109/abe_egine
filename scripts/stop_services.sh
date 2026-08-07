#!/usr/bin/env bash
#
# Run example:
#   ./scripts/services_stop.sh login gatehub gateway
# Command description:
#   Stop one or more project service processes in the current environment.
#
# Usage:
#   ./scripts/services_stop.sh [service...]
#
# Services:
#   gateway    Stop the gateway service.
#   login      Stop the login service.
#   gatehub    Stop the gatehub service.
#   all        Stop all default services. Currently: login, gatehub, gateway.
#
# Defaults:
#   service  login gatehub gateway
#   RUN_DIR  server/bin/run
#
# Environment:
#   GATEWAY_PID_FILE  Gateway pid file. Default: ${RUN_DIR}/gateway.pid
#   GATEWAY_OUT_FILE  Gateway stdout/stderr file. Default: server/bin/logs/gateway/stdout.log
#   GATEWAY_LOG_DIR   Gateway daily log root. Default: server/bin/logs/gateway
#   LOGIN_PID_FILE    Login pid file. Default: ${RUN_DIR}/login.pid
#   GATEHUB_PID_FILE  Gatehub pid file. Default: ${RUN_DIR}/gatehub.pid
#
# Note:
#   This script does not stop Docker containers. It stops service processes in
#   the current runtime environment; run it in the same environment that started
#   them.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  scripts/services_stop.sh [service...]

Services:
  gateway    Stop the gateway service.
  login      Stop the login service.
  gatehub    Stop the gatehub service.
  all        Stop all default services. Currently: login, gatehub, gateway.

Defaults:
  service  login gatehub gateway
  RUN_DIR  server/bin/run

Environment:
  GATEWAY_PID_FILE  Gateway pid file. Default: ${RUN_DIR}/gateway.pid
  GATEWAY_OUT_FILE  Gateway stdout/stderr file. Default: server/bin/logs/gateway/stdout.log
  GATEWAY_LOG_DIR   Gateway daily log root. Default: server/bin/logs/gateway
  LOGIN_PID_FILE    Login pid file. Default: ${RUN_DIR}/login.pid
  GATEHUB_PID_FILE  Gatehub pid file. Default: ${RUN_DIR}/gatehub.pid

This script does not stop Docker containers. It stops service processes in the
current runtime environment; run it in the same environment that started them.
EOF
}

pid_namespace() {
  readlink /proc/self/ns/pid 2>/dev/null || echo unknown
}

pid_file_value() {
  local key file

  key=$1
  file=$2
  sed -n "s/^${key}=//p" "${file}" 2>/dev/null | tail -n 1
}

pid_file_pid() {
  local file line

  file=$1
  line=$(pid_file_value pid "${file}")
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

stop_pid_file() {
  local service pid_file current_ns file_ns pid state i

  service=$1
  pid_file=$2

  if [ ! -f "${pid_file}" ]; then
    print_status "${service}" stopped -
    return 0
  fi

  pid=$(pid_file_pid "${pid_file}" || true)
  if [ -z "${pid}" ]; then
    rm -f "${pid_file}"
    print_status "${service}" invalid -
    return 0
  fi

  file_ns=$(pid_file_value pid_ns "${pid_file}")
  current_ns=$(pid_namespace)
  if [ -n "${file_ns}" ] && [ "${file_ns}" != "${current_ns}" ]; then
    echo "${service} pid file belongs to another PID namespace; run stop in the same environment" >&2
    exit 2
  fi

  state=$(process_state "${pid}")
  if [ -z "${state}" ]; then
    rm -f "${pid_file}"
    print_status "${service}" stopped "${pid}"
    return 0
  fi

  if [ "${state#Z}" != "${state}" ]; then
    rm -f "${pid_file}"
    print_status "${service}" exited "${pid}"
    return 0
  fi

  kill "${pid}" 2>/dev/null || true
  i=0
  while kill -0 "${pid}" 2>/dev/null; do
    state=$(process_state "${pid}")
    if [ -z "${state}" ] || [ "${state#Z}" != "${state}" ]; then
      break
    fi

    if [ "${i}" -ge 5 ]; then
      kill -9 "${pid}" 2>/dev/null || true
      break
    fi

    i=$((i + 1))
    sleep 1
  done

  rm -f "${pid_file}"
  print_status "${service}" stopped "${pid}"
}

stop_runtime_service() {
  local service service_key run_dir pid_file_var pid_file

  service=$1
  service_key=$(printf '%s' "${service}" | tr '[:lower:]' '[:upper:]')
  run_dir=${RUN_DIR:-server/bin/run}
  pid_file_var=${service_key}_PID_FILE
  pid_file=${!pid_file_var:-${run_dir}/${service}.pid}
  stop_pid_file "${service}" "${pid_file}"
}

stop_service() {
  local service

  service=$1
  case "${service}" in
    gateway|login|gatehub)
      stop_runtime_service "${service}"
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
  stop_service "${service}"
done < <(expand_services "$@")
