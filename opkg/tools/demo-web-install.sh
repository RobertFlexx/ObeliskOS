#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OPKG_DIR="${ROOT_DIR}/opkg"
OPKG_BIN="${OPKG_DIR}/opkg"

WORK_DIR="${TMPDIR:-/tmp}/opkg-web-demo"
REPO_DIR="${WORK_DIR}/repo"
PKG_DIR="${REPO_DIR}/packages"
ROOTFS="${WORK_DIR}/rootfs"
DBROOT="${ROOTFS}/var/lib/opkg"
REPOS_CONF="${ROOTFS}/etc/opkg/repos.conf"
PORT="${OPKG_DEMO_PORT:-18765}"
BASE_URL="http://127.0.0.1:${PORT}"

cleanup() {
  if [[ -n "${HTTP_PID:-}" ]]; then
    kill "${HTTP_PID}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

echo "[demo] building opkg host binary"
(
  cd "${OPKG_DIR}"
  dub build
)

echo "[demo] preparing workspace at ${WORK_DIR}"
rm -rf "${WORK_DIR}"
mkdir -p "${PKG_DIR}" "${ROOTFS}/etc/opkg"

echo "[demo] building sample package"
"${OPKG_BIN}" build "${OPKG_DIR}/examples/samplepkg" "${PKG_DIR}/hello-sample-1.0.0-x86_64.opk"

echo "[demo] generating index.json"
"${OPKG_BIN}" repo index "${REPO_DIR}"

echo "[demo] starting local repo web server on ${BASE_URL}"
python3 -m http.server "${PORT}" --directory "${REPO_DIR}" >/tmp/opkg-web-demo-http.log 2>&1 &
HTTP_PID=$!
sleep 1

cat > "${REPOS_CONF}" <<EOF
demo ${BASE_URL}
EOF

echo "[demo] running update/search/install over HTTP"
OPKG_ROOT="${ROOTFS}" OPKG_DB_ROOT="${DBROOT}" OPKG_REPOS_CONF="${REPOS_CONF}" \
  "${OPKG_BIN}" update
OPKG_ROOT="${ROOTFS}" OPKG_DB_ROOT="${DBROOT}" OPKG_REPOS_CONF="${REPOS_CONF}" \
  "${OPKG_BIN}" search hello-sample
OPKG_ROOT="${ROOTFS}" OPKG_DB_ROOT="${DBROOT}" OPKG_REPOS_CONF="${REPOS_CONF}" \
  "${OPKG_BIN}" install hello-sample

INSTALLED_FILE="${ROOTFS}/usr/share/doc/hello-sample.txt"
if [[ ! -f "${INSTALLED_FILE}" ]]; then
  echo "[demo] FAIL: expected file missing: ${INSTALLED_FILE}" >&2
  exit 1
fi

echo "[demo] installed payload contents:"
cat "${INSTALLED_FILE}"
echo "[demo] PASS: web install flow works"
