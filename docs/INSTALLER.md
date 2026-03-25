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

This release includes upgraded installer frontends and packaging into the
boot rootfs, plus a deterministic staging backend:

- validates and prompts for target disk, target root, hostname, and optional `/proc`
- explicit desktop mode choice: `tty`, `xorg`, `xfce`, or `xdm`
- optional desktop web repository URL and optional index pin (`sha256:<hash>`)
- creates target directory layout (`sbin`, `etc`, `var`, `tmp`, `dev`)
- copies core runtime binaries and AxiomFS policy files into target root
- writes install metadata to `/etc/obelisk-install.conf` (live + target)
- writes target repo config to `/etc/opkg/repos.conf` in target root
- writes staged desktop profile requests to
  `/etc/obelisk-package-profiles.conf` in target root
- retries `opkg update` and then installs selected desktop profiles
- includes `desktop-session` launcher in base image for post-install desktop bring-up

Disk partitioning, filesystem creation, and bootloader writes remain explicit
operator steps until block-device install paths are finalized.

## Desktop Bring-up Check

After selecting desktop profiles, run:

- `desktop-session auto --probe-only`

This validates framebuffer/input/VT readiness (`xorg-smoke`) and checks for
required desktop launcher binaries before attempting to start an X11 session.

Profile packages now install launcher compatibility wrappers under
`/usr/lib/obelisk/desktop`. Real `xinit`/`Xorg`/`startxfce4` binaries (if added
later) take precedence automatically without replacing package payloads.
`desktop-session xdm` is also supported and will use `xdm` or `xdm-compat`.

## Preparing a GitHub Pages Repo

If you are building custom Obelisk-native packages (for example: `xorg`,
`xfce`, `grep`, `sed`, editors, toolchains), you can stage a static web repo:

- `tools/repo/prepare-pages-repo.sh <packages-dir> <pages-repo-dir>`

This creates:

- `index.json` from package metadata
- `packages/` payload directory
- `index.html` and `README.md` for GitHub Pages publishing

Then use the published URL in installer desktop repo prompt.

## `obelisk-repo/` Template

This tree is included in the project root for direct upload to GitHub:

- `obelisk-repo/index.json` - package index consumed by `opkg update`
- `obelisk-repo/packages/` - upload `.opk` payloads here
- `obelisk-repo/recipes/*.opkrecipe` - recipe DSL files for contributors
- `obelisk-repo/scripts/generate-index.sh` - regenerate `index.json`
- `obelisk-repo/scripts/validate-recipes.sh` - local recipe lint pass

For the in-image static `opkg`, recipe DSL commands are now available:

- `opkg recipe scaffold <path.opkrecipe> <name>`
- `opkg recipe validate <path.opkrecipe>`
- `opkg recipe show <path.opkrecipe>`

## Philosophy Fit

- sysctl-first runtime management
- optional `/proc` compatibility selected during install
- minimal TTY-only deployment path for NAS/appliance usage
