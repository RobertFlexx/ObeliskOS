#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/diagnostics"
TIMEOUT_SEC="${1:-200}"

mkdir -p "${OUT_DIR}"
ts="$(date +%Y%m%d-%H%M%S)"
log_file="${OUT_DIR}/smoke-regression-${ts}.log"

echo "[smoke] root: ${ROOT_DIR}"
echo "[smoke] timeout: ${TIMEOUT_SEC}s"
echo "[smoke] log: ${log_file}"

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

prompt_markers = ("Obelisk osh ready.", "osh$ ", "$ ")
commands = [
    "ls",
    "ls -l /",
    "touch /tmp/smoke_file",
    "write /tmp/smoke_file \"hello smoke\"",
    "cat /tmp/smoke_file",
    "rm /tmp/smoke_file",
    "mkdir /tmp/smoke_dir",
    "rmdir /tmp/smoke_dir",
    "mkdir -p /tmp/mnt_smoke",
    "sudo mount -t devfs none /tmp/mnt_smoke",
    "ls /tmp/mnt_smoke",
    "sudo umount /tmp/mnt_smoke",
    "rmdir /tmp/mnt_smoke",
    "echo __MOUNT_SMOKE_OK__",
    "sysctl -a",
    "head /etc/motd",
    "wc /etc/motd",
    "users",
    "opkg --help",
    "echo __SMOKE_DONE__",
]

required = [
    "Obelisk osh ready.",
    "hello smoke",
    "system.kernel.version =",
    "Usage: opkg <command> [args]",
    "__SMOKE_DONE__",
    "__MOUNT_SMOKE_OK__",
]

forbidden = [
    "PAGE FAULT",
    "Kernel panic",
    "command not found",
    "mount: failed",
    "umount: failed",
    "sudo: authentication failure",
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
            tail = (tail + text)[-32768:]

            if not sent and any(marker in tail for marker in prompt_markers):
                try:
                    for cmd in commands:
                        proc.stdin.write((cmd + "\n").encode("utf-8"))
                        proc.stdin.flush()
                        time.sleep(0.25)
                    sent = True
                    sent_at = time.time()
                    print("[smoke] injected command sequence")
                except BrokenPipeError:
                    pass

        if sent and not sent_quit and sent_at is not None and (time.time() - sent_at) >= 12:
            try:
                proc.stdin.write(b"\x01x")  # Ctrl+A then x
                proc.stdin.flush()
                sent_quit = True
                print("[smoke] sent QEMU quit sequence")
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

with open(log_file, "rb") as f:
    text = f.read().decode("utf-8", errors="ignore")

missing = [s for s in required if s not in text]
found_forbidden = [s for s in forbidden if s in text]

if missing:
    print("[smoke] missing required output:", ", ".join(missing))
    sys.exit(2)
if found_forbidden:
    print("[smoke] found forbidden output:", ", ".join(found_forbidden))
    sys.exit(3)
if rc not in (0, 124):
    print(f"[smoke] run exited with status {rc}")
    sys.exit(4)

print("[smoke] PASS")
sys.exit(0)
PY
status=$?
set -e

if [ "${status}" -ne 0 ]; then
  echo "[smoke] FAILED (status ${status})"
  echo "[smoke] saved: ${log_file}"
  echo "${log_file}"
  exit "${status}"
fi

echo "[smoke] saved: ${log_file}"
echo "${log_file}"
