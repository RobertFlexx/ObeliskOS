#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <kernel-elf> <log-file>" >&2
  exit 1
fi

KERNEL_ELF="$1"
LOG_FILE="$2"

if [ ! -f "${KERNEL_ELF}" ]; then
  echo "error: kernel ELF not found: ${KERNEL_ELF}" >&2
  exit 1
fi
if [ ! -f "${LOG_FILE}" ]; then
  echo "error: log file not found: ${LOG_FILE}" >&2
  exit 1
fi

tmp_nm="$(mktemp)"
tmp_addrs="$(mktemp)"
trap 'rm -f "${tmp_nm}" "${tmp_addrs}"' EXIT

nm -n "${KERNEL_ELF}" > "${tmp_nm}"

awk '
/^RIP:/ {
  for (i = 1; i <= NF; i++) {
    if ($i ~ /^0x[0-9a-fA-F]+$/) print $i
  }
}
/^RAX:/ || /^RBX:/ || /^RCX:/ || /^RDX:/ || /^RSI:/ || /^RDI:/ || /^RBP:/ || /^RSP:/ || /^R8:/ || /^R9:/ || /^R10:/ || /^R11:/ || /^R12:/ || /^R13:/ || /^R14:/ || /^R15:/ {
  for (i = 1; i <= NF; i++) {
    if ($i ~ /^0x[0-9a-fA-F]+$/) print $i
  }
}
' "${LOG_FILE}" | awk '!seen[$0]++' > "${tmp_addrs}"

echo "=== Symbolication Report ==="
echo "kernel: ${KERNEL_ELF}"
echo "log:    ${LOG_FILE}"
echo

while IFS= read -r addr; do
  # Strip leading 0x and normalize.
  hex="${addr#0x}"
  # shellcheck disable=SC2059
  addr_dec="$(printf "%d" "0x${hex}" 2>/dev/null || true)"
  if [ -z "${addr_dec}" ]; then
    continue
  fi

  best_name=""
  best_delta=""
  while IFS= read -r line; do
    set -- ${line}
    [ $# -ge 3 ] || continue
    sym_hex="$1"
    sym_name="$3"
    sym_dec="$(printf "%d" "0x${sym_hex}" 2>/dev/null || true)"
    [ -n "${sym_dec}" ] || continue
    if [ "${sym_dec}" -le "${addr_dec}" ]; then
      best_name="${sym_name}"
      best_delta=$((addr_dec - sym_dec))
    fi
  done < "${tmp_nm}"

  if [ -n "${best_name}" ]; then
    printf "%s -> %s +0x%x\n" "${addr}" "${best_name}" "${best_delta}"
  fi
done < "${tmp_addrs}"
