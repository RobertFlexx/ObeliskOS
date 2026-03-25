#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
usage: import-linux-desktop-payloads.sh [options]

Options:
  --workspace <dir>    Obelisk workspace root (default: repo root)
  --source-root <dir>  Rootfs tree containing extracted binaries/libs (can repeat)
  --deb-dir <dir>      Directory with .deb files to extract and import
  --verify-only        Do not copy, only report missing required payloads
  -h, --help           Show this help

Examples:
  # Import from an already-extracted rootfs tree
  tools/desktop/import-linux-desktop-payloads.sh --source-root /tmp/xfce-root

  # Import directly from downloaded .deb files
  tools/desktop/import-linux-desktop-payloads.sh --deb-dir /tmp/deb-payloads
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace_default="$(cd "${script_dir}/../.." && pwd)"
workspace="${workspace_default}"
verify_only=0
deb_dir=""
declare -a source_roots=()
declare -a tmp_extract_dirs=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace)
      workspace="$2"
      shift 2
      ;;
    --source-root)
      source_roots+=("$2")
      shift 2
      ;;
    --deb-dir)
      deb_dir="$2"
      shift 2
      ;;
    --verify-only)
      verify_only=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -n "${deb_dir}" ]]; then
  if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "error: dpkg-deb is required for --deb-dir mode" >&2
    exit 1
  fi
  shopt -s nullglob
  debs=("${deb_dir}"/*.deb)
  shopt -u nullglob
  if [[ ${#debs[@]} -eq 0 ]]; then
    echo "error: no .deb files found in ${deb_dir}" >&2
    exit 1
  fi
  tmp_root="$(mktemp -d)"
  tmp_extract_dirs+=("${tmp_root}")
  for deb in "${debs[@]}"; do
    dpkg-deb -x "${deb}" "${tmp_root}"
  done
  source_roots+=("${tmp_root}")
fi

cleanup() {
  for d in "${tmp_extract_dirs[@]}"; do
    rm -rf "${d}"
  done
}
trap cleanup EXIT

if [[ ${#source_roots[@]} -eq 0 ]]; then
  echo "error: provide --source-root or --deb-dir" >&2
  exit 1
fi

pkg_desktop="${workspace}/opkg/examples/desktop-base/rootfs"
pkg_gtk="${workspace}/opkg/examples/gtk3-runtime/rootfs"
pkg_xorg="${workspace}/opkg/examples/xorg/rootfs"
pkg_xfce="${workspace}/opkg/examples/xfce-runtime/rootfs"
pkg_xdm="${workspace}/opkg/examples/xdm/rootfs"

for d in "${pkg_desktop}" "${pkg_gtk}" "${pkg_xorg}" "${pkg_xfce}" "${pkg_xdm}"; do
  if [[ ! -d "${d}" ]]; then
    echo "error: missing scaffold package root: ${d}" >&2
    exit 1
  fi
done

copy_from_root() {
  local root="$1"
  local rel="$2"
  local dst_root="$3"
  local src="${root}${rel}"
  local dst="${dst_root}${rel}"
  if [[ ! -e "${src}" ]]; then
    return 1
  fi
  if [[ -L "${src}" ]]; then
    local link_target
    link_target="$(readlink "${src}" || true)"
    if [[ -n "${link_target}" && "${link_target}" = /* && -e "${root}${link_target}" ]]; then
      src="${root}${link_target}"
    fi
  fi
  mkdir -p "$(dirname "${dst}")"
  # Dereference symlinks for dependency payloads so runtime paths
  # exist as real files inside package roots.
  cp -aL "${src}" "${dst}"
  return 0
}

copy_first_match() {
  local rel="$1"
  local dst_root="$2"
  local copied=1
  for r in "${source_roots[@]}"; do
    if copy_from_root "${r}" "${rel}" "${dst_root}"; then
      copied=0
      break
    fi
  done
  return "${copied}"
}

copy_dir_if_exists() {
  local rel="$1"
  local dst_root="$2"
  for r in "${source_roots[@]}"; do
    local src="${r}${rel}"
    if [[ -d "${src}" ]]; then
      mkdir -p "${dst_root}${rel}"
      cp -a "${src}/." "${dst_root}${rel}/"
      return 0
    fi
  done
  return 1
}

resolve_soname_path() {
  local soname="$1"
  local root="$2"
  local base
  for base in \
    /lib64 \
    /usr/lib64 \
    /lib \
    /usr/lib \
    /lib/x86_64-linux-gnu \
    /usr/lib/x86_64-linux-gnu; do
    if [[ -e "${root}${base}/${soname}" ]]; then
      echo "${base}/${soname}"
      return 0
    fi
  done
  return 1
}

classify_lib_package_root() {
  local soname="$1"
  if [[ "${soname}" =~ ^libgtk-3\.so|^libgdk-3\.so|^libglib-2\.0\.so|^libgobject-2\.0\.so|^libgio-2\.0\.so|^libpango|^libcairo\.so|^libatk|^libgmodule|^libgthread ]]; then
    echo "${pkg_gtk}"
  else
    echo "${pkg_desktop}"
  fi
}

declare -A seen_elf=()

queue_push_unique() {
  local path="$1"
  local -n qref="$2"
  if [[ -z "${seen_elf[$path]+x}" ]]; then
    seen_elf["$path"]=1
    qref+=("$path")
  fi
}

collect_binary_deps() {
  local abs_bin="$1"
  local root="$2"

  local queue=()
  queue_push_unique "${abs_bin}" queue

  while [[ ${#queue[@]} -gt 0 ]]; do
    local current="${queue[0]}"
    queue=("${queue[@]:1}")

    # PT_INTERP loader path
    local interp
    interp="$(readelf -l "${current}" 2>/dev/null | awk '/interpreter/ {print $NF}' | tr -d '[]' || true)"
    if [[ -n "${interp}" ]]; then
      copy_from_root "${root}" "${interp}" "${pkg_desktop}" || copy_first_match "${interp}" "${pkg_desktop}" || true
      if [[ -e "${root}${interp}" ]]; then
        queue_push_unique "${root}${interp}" queue
      fi
    fi

    while IFS= read -r soname; do
      [[ -n "${soname}" ]] || continue
      local rel
      rel="$(resolve_soname_path "${soname}" "${root}" || true)"
      if [[ -z "${rel}" ]]; then
        continue
      fi
      local dst_pkg
      dst_pkg="$(classify_lib_package_root "${soname}")"
      copy_from_root "${root}" "${rel}" "${dst_pkg}" || true
      if [[ -e "${root}${rel}" ]]; then
        queue_push_unique "${root}${rel}" queue
      fi
    done < <(readelf -d "${current}" 2>/dev/null | awk '/NEEDED/ {gsub(/\[|\]/,"",$5); print $5}')
  done
}

import_from_root() {
  local root="$1"
  echo "==> importing desktop payloads from ${root}"

  local xorg_bins=(
    /usr/bin/X
    /usr/bin/Xorg
    /usr/lib/xorg/Xorg
    /usr/bin/xinit
  )
  local xfce_bins=(
    /usr/bin/xfce4-session
    /usr/bin/startxfce4
    /usr/bin/xfwm4
    /usr/bin/xfce4-panel
  )
  local xdm_bins=(
    /usr/bin/xdm
  )

  for rel in "${xorg_bins[@]}"; do
    if [[ -x "${root}${rel}" ]]; then
      if [[ ${verify_only} -eq 0 ]]; then
        copy_from_root "${root}" "${rel}" "${pkg_xorg}" || true
      fi
      collect_binary_deps "${root}${rel}" "${root}"
    fi
  done

  for rel in "${xfce_bins[@]}"; do
    if [[ -x "${root}${rel}" ]]; then
      if [[ ${verify_only} -eq 0 ]]; then
        copy_from_root "${root}" "${rel}" "${pkg_xfce}" || true
      fi
      collect_binary_deps "${root}${rel}" "${root}"
    fi
  done

  for rel in "${xdm_bins[@]}"; do
    if [[ -x "${root}${rel}" ]]; then
      if [[ ${verify_only} -eq 0 ]]; then
        copy_from_root "${root}" "${rel}" "${pkg_xdm}" || true
      fi
      collect_binary_deps "${root}${rel}" "${root}"
    fi
  done

  # Ensure PT_INTERP target path used by Debian desktop binaries exists.
  if [[ ${verify_only} -eq 0 && -e "${root}/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ]]; then
    mkdir -p "${pkg_desktop}/lib64"
    cp -aL "${root}/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
      "${pkg_desktop}/lib64/ld-linux-x86-64.so.2"
  fi

  if [[ ${verify_only} -eq 0 ]]; then
    # Keep runtime payload minimal so package install is reliable
    # in constrained guests; xorg scaffold already ships xdm configs.
    copy_dir_if_exists "/etc/X11/xinit" "${pkg_xorg}" || true
    copy_dir_if_exists "/etc/xdg/xfce4" "${pkg_xfce}" || true
    copy_dir_if_exists "/usr/share/xfce4" "${pkg_xfce}" || true
  fi
}

for r in "${source_roots[@]}"; do
  import_from_root "${r}"
done

missing=0
required_runtime=(
  "${pkg_xorg}/usr/bin/X"
  "${pkg_xorg}/usr/bin/xinit"
  "${pkg_xfce}/usr/bin/xfce4-session"
  "${pkg_xdm}/usr/bin/xdm"
)

echo "==> verification"
for f in "${required_runtime[@]}"; do
  if [[ ! -x "${f}" ]]; then
    echo "missing: ${f}"
    missing=1
  else
    echo "ok: ${f}"
  fi
done

if [[ ${missing} -ne 0 ]]; then
  echo "Desktop payload import incomplete."
  exit 2
fi

echo "Desktop payload import complete."
echo "Next: make iso && boot && run: opkg install-profile xorg && opkg install-profile xfce && opkg install-profile xdm && desktop-session xdm"
