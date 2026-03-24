#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/diagnostics"
TIMEOUT_SEC="${1:-180}"

mkdir -p "${OUT_DIR}"
ts="$(date +%Y%m%d-%H%M%S)"
log_file="${OUT_DIR}/run-serial-${ts}.log"

echo "[diag] root: ${ROOT_DIR}"
echo "[diag] timeout: ${TIMEOUT_SEC}s"
echo "[diag] log: ${log_file}"

cd "${ROOT_DIR}"

# Capture full serial output for offline triage.
set +e
timeout --signal=INT "${TIMEOUT_SEC}" make run-serial >"${log_file}" 2>&1
status=$?
set -e

if [ "${status}" -eq 124 ]; then
  echo "[diag] capture timed out after ${TIMEOUT_SEC}s (expected for long interactive sessions)"
elif [ "${status}" -ne 0 ]; then
  echo "[diag] make run-serial exited with status ${status}"
else
  echo "[diag] capture completed successfully"
fi

echo "[diag] saved: ${log_file}"
echo "${log_file}"
