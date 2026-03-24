# Static D Userland Plan (Obelisk)

This document defines the current practical path to run D binaries natively in
Obelisk **without** enabling dynamic ELF support.

## Goal

Run D-built userland binaries under the same static execution model as existing
Obelisk userland tools (`init`, `busybox`, etc.), while keeping runtime
requirements minimal and explicit.

## Current validated mode

- Compiler mode: D `-betterC`
- Link model: static (`-nostdlib -static -no-pie`, linked via userland Makefile)
- Startup model: explicit `_start` entrypoint (or C startup shim)
- Runtime assumptions: no druntime/Phobos initialization

## Phase 1 status: minimal D native execution proof

Implemented probe binary:

- `/bin/dprobe` (source: `userland/bin/dprobe/dprobe.d`)

Behavior:

- prints `dprobe: static D userspace execution OK`
- exits cleanly

This proves static D object + Obelisk libc + static link path.

## Phase 2 runtime/toolchain audit (minimum requirements)

What full-host D builds currently expect (and why they do not run in current
Obelisk static mode):

- druntime startup and module initialization
- allocator/GC runtime setup
- exception/unwind machinery
- TLS assumptions
- libc/loader behavior typical of Linux dynamic userspace

What current Obelisk static D mode supports:

- plain `extern(C)` function model
- explicit startup glue
- direct use of Obelisk libc wrappers/syscalls
- no implicit druntime/Phobos requirements

## Phase 3 build adaptation rules

For Obelisk-native D binaries today:

1. write D in `-betterC` style
2. avoid Phobos/druntime dependencies
3. provide explicit entrypoint contract (`_start` path)
4. link through existing static userland link rules

## Phase 4 opkg transition status

- `/bin/opkg` now uses D command logic (`userland/bin/opkg/opkg_d.d`) in static mode
- startup/argc glue remains in small C shim (`userland/bin/opkg/opkg.c`)
- in-image static D `opkg` currently supports practical local package workflows:
  - `install <file.opk>`
  - `remove <pkg>`
  - `list`, `info <pkg>`, `files <pkg>`, `owner <path>`
- repo/network commands (`update`, `search`, repo-name install) still require
  additional runtime bring-up in this profile
- `build <dir>` remains intentionally unavailable in this profile for now
- this moves `/bin/opkg` from help-only routing to usable local package management

## Remaining blockers to full feature-parity D opkg in-image

To run the complete host-side D `opkg` implementation unchanged inside Obelisk,
the system still needs one of:

1. static druntime/Phobos-compatible userspace support, or
2. dynamic ELF loader/runtime path (currently intentionally disabled), or
3. progressive port of `opkg` modules to `-betterC`-compatible subset.

Near-term preferred route:

- continue progressive port of `opkg` core modules toward `-betterC`
- keep binary static and runtime-minimal
- avoid enabling experimental dynamic ELF for this path
