# Obelisk OS

**From Axioms, Order.**

Obelisk is a standalone Unix-like OS focused on a practical minimal base system
that is CLI-first and desktop-capable.

## Official Target Profile

Obelisk is converging on one clear daily-usable path:

- minimal, stable Unix-like base system
- official desktop environment: **XFCE**
- official display/login manager: **XDM**
- official package manager: **opkg**
- package ecosystem: **binary-first `.opk` delivery**
- single supported desktop path first (no KDE/GNOME parity work in this phase)

This project values coherence and reliability over feature count.

## System Philosophy

- Keep the base system small, auditable, and recoverable
- Prefer traditional Unix behavior where practical
- Keep TTY shell as the safe fallback path
- Treat desktop support as an extension of a stable base, not a separate OS
- Ship practical milestones, avoid speculative mega-abstractions

## Current Architecture

- **Kernel**: monolithic x86_64 kernel in C with assembly bootstrap paths
- **Filesystem**: AxiomFS and VFS/devfs stack
- **Control surface**: `sysctl`-first operational model
- **Userland**: small libc + compact base tools + `rockbox` shell/tool multiplexer
- **Packages**: native `opkg` and `.opk` repository flow

***OBELISK OPKG IS NOT OPENWRT OPKG!***
## Repository Layout

- `kernel/` - kernel source
- `userland/` - user programs, libc, init/session tools
- `opkg/` - package manager and package examples
- `docs/` - project direction, roadmap, and release docs
- `rootfs-overlay/` - optional rootfs overlay content
- `Makefile` - top-level build and ISO packaging

## Build Prerequisites

- `x86_64-elf-gcc` toolchain (or `CROSS_COMPILE=<prefix>`)
- GNU Make
- GRUB 2 tooling (`grub-mkrescue`)
- QEMU
- `tar`

To build a cross-compiler with the helper script:

```bash
./tools/mkaxiomfs/cross-compile.sh
export PATH="$HOME/opt/cross/bin:$PATH"
```

## Build and Run

Build full artifact set:

```bash
make
```

Component builds:

```bash
make kernel
make userland
make rootfs
make iso
```

Run:

```bash
make run
make run-gui
make run-kvm
```

Debug boot:

```bash
make debug
```

Clean:

```bash
make clean
```

## Roadmap and Policy Docs

- Main phased plan: `docs/roadmap.md`
- Desktop/XDM path: `docs/desktop-roadmap.md`
- Packaging policy and package waves: `docs/packaging-policy.md`
- Release gate checklist: `docs/RELEASE_CHECKLIST.md`
- Installer details: `docs/INSTALLER.md`

## Release Direction

Obelisk is pre-1.0. "Release-ready" currently means:

- reproducible builds and installable artifacts
- stable boot/session fallback behavior
- predictable package install/update/remove flow
- documented known gaps and validation gates


bigguy118 is now tester
