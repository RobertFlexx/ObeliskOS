# Obelisk Packaging Policy (opkg/.opk)

This document defines package-system policy for the XFCE-first Obelisk path.

## Core Policy

- `opkg` is the official package manager.
- `.opk` is the official package format.
- Binary-first delivery is the default.
- Source/recipe support is optional and later; not a blocker for first XFCE boot.
- Packaging must prioritize deterministic install/remove behavior over solver complexity.

## Repository Group Taxonomy

Official groups:

- `core` - base OS runtime and essential CLI tools
- `devel` - compiler/toolchain/build/debug utilities
- `extra` - optional non-essential utilities
- `desktop` - desktop prerequisites shared across graphical stack
- `xfce` - official desktop session packages and dependencies

## Web Repository Layout (Official)

Recommended static layout for hosted repositories:

```text
repo/
  index.json
  groups.json
  packages/
    <name>-<version>-<arch>.opk
  checksums/
    SHA256SUMS
```

Minimum index metadata per package:

- name, version, arch
- summary/section/group
- dependency list
- checksum
- package URL/path

## opkg Capability Baseline for XFCE Path

Required practical command set:

- `opkg update`
- `opkg search`
- `opkg install`
- `opkg remove`
- `opkg info`
- `opkg files`
- `opkg owner`

Non-goals for this milestone phase:

- SAT-grade dependency solver
- complex transactional rollback engine
- source-package feature parity

## Package Wave Order (Practical Path)

1. **Core Unix tools**
   - shell/coreutils-class basics
   - init/service/runtime utilities
2. **Devel/build tools**
   - compiler/binutils/build helpers needed for ports/tooling
3. **Networking/fetch tools**
   - DNS/fetch/curl-like utilities needed by opkg and diagnostics
4. **Desktop prerequisites**
   - base fonts/libs/session prerequisites and X11 support runtime deps
5. **Xorg + XDM**
   - display server and official login manager path
6. **XFCE core**
   - session manager, panel, terminal, file manager, settings essentials

## Desktop Package Grouping Rules

- keep session-critical components in `xfce` group
- keep reusable graphical runtime pieces in `desktop`
- avoid scattering core desktop dependencies into `extra`
- package split should reflect operational recovery:
  - if `xfce` fails, `core` + TTY still fully usable

## Binary Build and Delivery Policy

- Prefer Obelisk-built binaries when feasible and reproducible.
- Allow imported binaries as bootstrap if:
  - integrity is verified
  - runtime deps are explicit
  - package tests pass on target image
- Treat imported binaries as transitional until native/reproducible builds exist.

## Desktop Package QA Requirements

For every desktop package wave:

- install from clean image
- verify all runtime dependencies resolved
- verify file ownership/manifests for clean remove
- verify `desktop-session xdm` regression path
- verify fallback-to-TTY behavior remains intact

## Packaging "Done" Criteria for First Daily-Usable Desktop

- `opkg` command baseline is reliable on both TTY and desktop installs
- `xorg`, `xdm`, `xfce` profiles install cleanly from official repo
- package removal does not orphan critical runtime files
- upgrades do not break TTY fallback path
