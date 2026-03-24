#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OPKG_BIN="${ROOT_DIR}/opkg/opkg"
REPO_DIR="${1:-${ROOT_DIR}/opkg/repo-site}"
PKG_DIR="${REPO_DIR}/packages"

mkdir -p "${PKG_DIR}"

echo "[repo] building host opkg"
( cd "${ROOT_DIR}/opkg" && dub build )

echo "[repo] building sample packages"
"${OPKG_BIN}" build "${ROOT_DIR}/opkg/examples/samplepkg" "${PKG_DIR}/hello-sample-1.0.0-x86_64.opk"
"${OPKG_BIN}" build "${ROOT_DIR}/opkg/examples/grep" "${PKG_DIR}/grep-1.0.0-x86_64.opk"
"${OPKG_BIN}" build "${ROOT_DIR}/opkg/examples/sed" "${PKG_DIR}/sed-1.0.0-x86_64.opk"

echo "[repo] regenerating index.json"
"${OPKG_BIN}" repo index "${REPO_DIR}"

echo "[repo] done: ${REPO_DIR}"
echo "[repo] publish this directory to GitHub Pages"
