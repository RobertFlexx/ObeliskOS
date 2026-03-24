#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <log-file>" >&2
  exit 1
fi

LOG_FILE="$1"
if [ ! -f "${LOG_FILE}" ]; then
  echo "error: log file not found: ${LOG_FILE}" >&2
  exit 1
fi

awk '
BEGIN {
  in_block = 0
  block = ""
  saw_block = 0
}
/^=== PAGE FAULT ===$/ || /^=== EXCEPTION:/ {
  in_block = 1
  block = $0 "\n"
  next
}
in_block {
  block = block $0 "\n"
  if ($0 ~ /^========================================$/) {
    # Keep collecting; full panic block usually has multiple separators.
  }
  if ($0 ~ /^System halted\./ || $0 ~ /^PANIC:/ || $0 ~ /^User exception in pid=/) {
    saw_block = 1
  }
  if (saw_block && $0 ~ /^========================================$/) {
    last = block
    in_block = 0
    saw_block = 0
    block = ""
  }
}
END {
  if (length(last) == 0) {
    print "No panic/exception block found."
    exit 2
  }
  print last
}
' "${LOG_FILE}"
