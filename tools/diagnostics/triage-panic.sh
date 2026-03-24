#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CAPTURE_SCRIPT="${ROOT_DIR}/tools/diagnostics/capture-run-serial.sh"
REPRO_SCRIPT="${ROOT_DIR}/tools/diagnostics/repro-run-serial.sh"
EXTRACT_SCRIPT="${ROOT_DIR}/tools/diagnostics/extract-panic.sh"
SYMBOLICATE_SCRIPT="${ROOT_DIR}/tools/diagnostics/symbolicate-panic.sh"
KERNEL_ELF="${ROOT_DIR}/build/kernel.elf"
TIMEOUT_SEC="${1:-180}"
MODE="${MODE:-capture}"

if [ ! -f "${CAPTURE_SCRIPT}" ] || [ ! -f "${REPRO_SCRIPT}" ] || [ ! -f "${EXTRACT_SCRIPT}" ] || [ ! -f "${SYMBOLICATE_SCRIPT}" ]; then
  echo "error: diagnostics scripts are missing" >&2
  exit 1
fi

if [ "${MODE}" = "repro" ]; then
  echo "[triage] running repro serial capture..."
  LOG_FILE="$(bash "${REPRO_SCRIPT}" "${TIMEOUT_SEC}" | tail -n 1)"
else
  echo "[triage] capturing serial run..."
  LOG_FILE="$(bash "${CAPTURE_SCRIPT}" "${TIMEOUT_SEC}" | tail -n 1)"
fi

if [ ! -f "${LOG_FILE}" ]; then
  echo "error: expected log file not found: ${LOG_FILE}" >&2
  exit 1
fi

echo
echo "[triage] last panic/exception block"
echo "--------------------------------"
set +e
bash "${EXTRACT_SCRIPT}" "${LOG_FILE}"
extract_status=$?
set -e

echo
echo "[triage] symbolication"
echo "----------------------"
bash "${SYMBOLICATE_SCRIPT}" "${KERNEL_ELF}" "${LOG_FILE}" || true

echo
echo "[triage] log file: ${LOG_FILE}"
if [ "${extract_status}" -ne 0 ]; then
  echo "[triage] no panic block detected in capture"
fi
