#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <rootfs-dir> <optional-shell-bin> [coreutils-bin...]" >&2
  exit 1
fi

ROOTFS_DIR="$1"
SHELL_BIN="$2"
shift 2

copy_path_into_rootfs() {
  local src="$1"
  local dst="$ROOTFS_DIR$src"

  if [ ! -e "$src" ]; then
    return 0
  fi

  mkdir -p "$(dirname "$dst")"
  cp -L "$src" "$dst"

  # x86_64 guest loader commonly searches /lib64 and /usr/lib64 first.
  # Some hosts (e.g. Solus) place 64-bit libs under /usr/lib, so mirror them.
  case "$src" in
    /usr/lib/*.so*|/lib/*.so*)
      local base compat_dst
      base="$(basename "$src")"
      if [[ "$src" == /usr/lib/* ]]; then
        compat_dst="$ROOTFS_DIR/usr/lib64/$base"
      else
        compat_dst="$ROOTFS_DIR/lib64/$base"
      fi
      mkdir -p "$(dirname "$compat_dst")"
      cp -L "$src" "$compat_dst"
      ;;
  esac
}

copy_runtime_deps() {
  local bin="$1"

  if [ ! -x "$bin" ]; then
    return 0
  fi

  local interp
  interp="$(readelf -l "$bin" 2>/dev/null | awk '/interpreter/ {print $NF}' | tr -d '[]' || true)"
  if [ -n "${interp:-}" ] && [ -e "$interp" ]; then
    copy_path_into_rootfs "$interp"
  fi

  while IFS= read -r dep; do
    [ -n "$dep" ] || continue
    [ -e "$dep" ] || continue
    copy_path_into_rootfs "$dep"
  done < <(ldd "$bin" 2>/dev/null | awk '{for (i=1;i<=NF;i++) if ($i ~ /^\//) print $i}' | sort -u)
}

import_bin() {
  local src="$1"
  if [ ! -x "$src" ]; then
    return 1
  fi
  copy_path_into_rootfs "$src"
  copy_runtime_deps "$src"
  return 0
}

echo "Importing host userland into ${ROOTFS_DIR}..."

if [ -n "${SHELL_BIN}" ] && import_bin "$SHELL_BIN"; then
  echo "  imported shell: $SHELL_BIN"
fi

for tool in "$@"; do
  if import_bin "/usr/bin/$tool"; then
    echo "  imported coreutils: /usr/bin/$tool"
    continue
  fi
  if import_bin "/bin/$tool"; then
    echo "  imported coreutils: /bin/$tool"
  fi
done
