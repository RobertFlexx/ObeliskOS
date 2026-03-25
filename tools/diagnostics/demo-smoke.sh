#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/diagnostics"
TIMEOUT_SEC="${1:-180}"

mkdir -p "${OUT_DIR}"
ts="$(date +%Y%m%d-%H%M%S)"
log_file="${OUT_DIR}/demo-smoke-${ts}.log"

echo "[demo] root: ${ROOT_DIR}"
echo "[demo] timeout: ${TIMEOUT_SEC}s"
echo "[demo] log: ${log_file}"

set +e
python3 - "${ROOT_DIR}" "${log_file}" "${TIMEOUT_SEC}" <<'PY'
import os
import select
import signal
import subprocess
import sys
import time

root_dir = sys.argv[1]
log_file = sys.argv[2]
timeout_sec = int(sys.argv[3])

commands = [
    "help",
    "ls",
    "ls -l /tmp",
    "write /tmp/demo \"hello from demo\"",
    "cat /tmp/demo",
    "wc /tmp/demo",
    "head /tmp/demo",
    "sysctl system.kernel.version",
    "sysctl system.memory.total",
    "users",
]

proc = subprocess.Popen(
    ["make", "run-serial"],
    cwd=root_dir,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    bufsize=0,
)

start = time.time()
tail = ""
sent = False
sent_at = None
sent_quit = False

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
            tail = (tail + text)[-16384:]

            if not sent and ("Obelisk osh ready." in tail or "osh$ " in tail or "$ " in tail):
                try:
                    for cmd in commands:
                        proc.stdin.write((cmd + "\n").encode("utf-8"))
                        proc.stdin.flush()
                        time.sleep(0.35)
                    sent = True
                    sent_at = time.time()
                    print("[demo] injected demo commands")
                except BrokenPipeError:
                    pass

        if sent and not sent_quit and sent_at is not None and (time.time() - sent_at) >= 4:
            try:
                proc.stdin.write(b"\x01x")  # Ctrl+A then x
                proc.stdin.flush()
                sent_quit = True
                print("[demo] sent QEMU quit sequence")
            except BrokenPipeError:
                pass

try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    try:
        proc.kill()
    except Exception:
        pass
PY
status=$?
set -e

if [ "${status}" -ne 0 ]; then
  echo "[demo] run exited with status ${status}"
fi

echo "[demo] saved: ${log_file}"
echo "${log_file}"
