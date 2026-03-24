# Obelisk OS

**From Axioms, Order.**

Obelisk is a standalone, Unix-like operating system project focused on a stable
TTY/CLI environment with a policy-driven filesystem architecture.

## Project Philosophy

- Keep the base system simple and auditable
- Make policy declarative and runtime-adjustable
- Favor `sysctl` as the default observability/control plane
- Treat `/proc` as optional compatibility surface, not a hard dependency
- Optimize for always-on appliance usage (including NAS-style deployments)

## Current Architecture

- **Kernel**: monolithic x86_64 kernel in C with a small assembly bootstrap
- **AxiomFS**: policy-aware filesystem under `kernel/fs/axiomfs`
- **sysctl tree**: kernel-managed settings and runtime state under `kernel/sysctl`
- **Policy daemon model**: Prolog sources under `userland/axiomd`
- **Minimal userland**:
  - `userland/init/init.c` (`/sbin/init`)
  - `userland/bin/sysctl/sysctl.c` (`/sbin/sysctl`)
  - `userland/bin/installer/installer.c` (`/sbin/installer`, CLI flow)
  - `userland/bin/installer-tui/installer_tui.c` (`/sbin/installer-tui`, TUI flow)
  - tiny libc in `userland/libc`

## Repository Layout

- `kernel/` - kernel source
- `userland/` - user programs, libc, policy daemon sources
- `tools/mkaxiomfs/` - toolchain helper scripts
- `grub.cfg` - boot menu entries
- `Makefile` - top-level build and ISO packaging

## Build Prerequisites

- `x86_64-elf-gcc` toolchain (or `CROSS_COMPILE=<prefix>` equivalent)
- GNU Make
- GRUB 2 tooling (`grub-mkrescue`)
- QEMU for testing
- `tar`

To build a cross-compiler with the provided helper:

```bash
./tools/mkaxiomfs/cross-compile.sh
export PATH="$HOME/opt/cross/bin:$PATH"
```

## Build and Run

Build everything (kernel + userland + packaged rootfs + ISO):

```bash
make
```

Build only specific components:

```bash
make kernel
make userland
make rootfs
make iso
```

Run in QEMU:

```bash
make run
```

If serial logs appear but the QEMU window is blank, make sure you are using the
updated ISO (GRUB now forces text payload for VGA console output), then retry
`make run`. Kernel logs are mirrored to serial and VGA text mode.

Debug boot with GDB stub:

```bash
make debug
```

Clean build artifacts:

```bash
make clean
```

## What "Release Ready" Means Here

This repository is still pre-1.0 and under active development. "Release quality"
for Obelisk currently means:

- reproducible builds
- consistent artifact packaging
- clear documentation of current behavior and known gaps
- no known build-system breakages in default paths

See `docs/RELEASE_CHECKLIST.md` and `docs/ROADMAP.md` for planned milestones.
Installer details are documented in `docs/INSTALLER.md`.

## Optional Userland Overlay

You can ship extra tools (for example BusyBox, selected GNU tools, or `zsh`) by
placing binaries and config files in `rootfs-overlay/`. The build copies this
overlay into the packaged root filesystem.

See `rootfs-overlay/README.txt` for expected layout and notes.

Current default userland direction is BusyBox-style for bring-up simplicity:
the image now includes `/bin/busybox`, `/bin/sh`, and `/bin/zsh` (zsh is
provided via BusyBox applet compatibility in this stage).
