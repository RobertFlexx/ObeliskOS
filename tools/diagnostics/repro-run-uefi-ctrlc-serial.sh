#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/diagnostics"
TIMEOUT_SEC="${1:-240}"
POST_CMD_WAIT_SEC="${POST_CMD_WAIT_SEC:-5}"
REPRO_CMD="${REPRO_CMD:-ping -c 100 1.1.1.1}"
CTRL_C_AFTER_SEC="${CTRL_C_AFTER_SEC:-2}"

mkdir -p "${OUT_DIR}"
ts="$(date +%Y%m%d-%H%M%S)"
log_file="${OUT_DIR}/repro-uefi-ctrlc-serial-${ts}.log"

echo "[uefi-ctrlc-repro] root: ${ROOT_DIR}"
echo "[uefi-ctrlc-repro] timeout: ${TIMEOUT_SEC}s"
echo "[uefi-ctrlc-repro] command: ${REPRO_CMD}"
echo "[uefi-ctrlc-repro] ctrl-c after: ${CTRL_C_AFTER_SEC}s"
echo "[uefi-ctrlc-repro] log: ${log_file}"

set +e
python3 - "${ROOT_DIR}" "${log_file}" "${TIMEOUT_SEC}" "${REPRO_CMD}" "${POST_CMD_WAIT_SEC}" "${CTRL_C_AFTER_SEC}" <<'PY'
import os
import select
import signal
import subprocess
import sys
import time

root_dir = sys.argv[1]
log_file = sys.argv[2]
timeout_sec = int(sys.argv[3])
repro_cmd = sys.argv[4]
post_cmd_wait_sec = int(sys.argv[5])
ctrl_c_after_sec = int(sys.argv[6])

prompt_markers = (
    "Obelisk osh ready.",
    "$ ",
    "osh$ ",
)

proc = subprocess.Popen(
    ["make", "run-uefi"],
    cwd=root_dir,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    bufsize=0,
)

start = time.time()
sent_cmd = False
sent_ctrlc = False
sent_quit = False
cmd_sent_at = None
tail = ""
prompt_after_ctrlc_seen = False

with open(log_file, "wb") as log:
    while True:
        now = time.time()
        if now - start > timeout_sec:
            try:
                proc.send_signal(signal.SIGINT)
            except Exception:
                pass
            break

        if proc.poll() is not None:
            break

        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if proc.stdout in ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                continue
            log.write(chunk)
            log.flush()
            text = chunk.decode("utf-8", errors="ignore")
            tail = (tail + text)[-8192:]

            if not sent_cmd and any(marker in tail for marker in prompt_markers):
                try:
                    proc.stdin.write((repro_cmd + "\n").encode("utf-8"))
                    proc.stdin.flush()
                    sent_cmd = True
                    cmd_sent_at = time.time()
                    print(f"[uefi-ctrlc-repro] injected command: {repro_cmd}")
                except BrokenPipeError:
                    pass

            if sent_cmd and not sent_ctrlc and cmd_sent_at is not None and (time.time() - cmd_sent_at) >= ctrl_c_after_sec:
                try:
                    # Send the physical Ctrl-C character (0x03) to the guest UART.
                    proc.stdin.write(b"\x03")
                    proc.stdin.flush()
                    sent_ctrlc = True
                    print("[uefi-ctrlc-repro] sent 0x03 to guest")
                except BrokenPipeError:
                    pass

            if sent_ctrlc and not prompt_after_ctrlc_seen and any(marker in tail for marker in prompt_markers):
                prompt_after_ctrlc_seen = True
                print("[uefi-ctrlc-repro] prompt returned after Ctrl-C")
                # QEMU mux quit sequence (Ctrl+A then x).
                try:
                    proc.stdin.write(b"\x01x")
                    proc.stdin.flush()
                    sent_quit = True
                    print("[uefi-ctrlc-repro] sent QEMU quit sequence")
                except BrokenPipeError:
                    pass

rc = None
try:
    rc = proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    try:
        proc.kill()
    except Exception:
        pass
    rc = 124

if rc is None:
    rc = 1
sys.exit(rc)
PY

status=$?
set -e

if [ "${status}" -eq 124 ]; then
  echo "[uefi-ctrlc-repro] timed out after ${TIMEOUT_SEC}s"
elif [ "${status}" -ne 0 ]; then
  echo "[uefi-ctrlc-repro] run exited with status ${status}"
else
  echo "[uefi-ctrlc-repro] run completed"
fi

echo "[uefi-ctrlc-repro] saved: ${log_file}"
echo "${log_file}"

