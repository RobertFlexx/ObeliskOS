# Obelisk Installer Modes

Obelisk ships two installer entrypoints in the ISO rootfs:

- `/sbin/installer` - OpenBSD-style interactive CLI prompts
- `/sbin/installer-tui` - FreeBSD-style text menu (TUI)

Both frontends target the same long-term backend workflow:

1. disk detection
2. partition layout confirmation
3. filesystem creation and base install
4. bootloader setup
5. post-install configuration (including optional `/proc`)

## Current Status

This release includes functional installer frontends and packaging into the
boot rootfs, plus a deterministic staging backend:

- prompts for target disk, hostname, optional `/proc`, and target root path
- creates target directory layout (`sbin`, `etc`, `var`, `tmp`, `dev`)
- copies core runtime binaries and AxiomFS policy files into target root
- writes install metadata to `/etc/obelisk-install.conf` (live + target)

Disk partitioning, filesystem creation, and bootloader writes remain explicit
operator steps until block-device install paths are finalized.

## Philosophy Fit

- sysctl-first runtime management
- optional `/proc` compatibility selected during install
- minimal TTY-only deployment path for NAS/appliance usage
