# XFCE Path Implementation Scaffold

Use this file as the execution tracker for the phased plan in:

- `docs/roadmap.md`
- `docs/desktop-roadmap.md`
- `docs/packaging-policy.md`

## Phase Tracker

- [ ] Phase A complete (solid base OS)
- [ ] Phase B complete (packaging ecosystem baseline)
- [ ] Phase C complete (desktop runtime prerequisites)
- [ ] Phase D complete (graphics/input/windowing substrate)
- [ ] Phase E complete (XDM login path)
- [ ] Phase F complete (XFCE package wave)
- [ ] Phase G complete (daily-usable polish)

## Current Primary Milestone

- [ ] XDM safe boot with guaranteed TTY fallback
- [ ] `desktop_mode=xdm` retry/fallback policy documented and enforced
- [ ] first reproducible "real XFCE boot attempt" regression script

## Runtime Blocker Checklist (for Phase C)

- [ ] dynamic ELF + loader ABI gaps closed for `xinit` and `xdm`
- [ ] PTY/session/process semantics validated for login/session chain
- [ ] local IPC requirements satisfied (`AF_UNIX` as needed)
- [ ] `poll`/`select` readiness validated with desktop daemons
- [ ] libc/runtime env/layout compatibility gaps documented and fixed

## Packaging Wave Tracker

### Wave 1: core

- [ ] core tools packaged in `core`
- [ ] install/remove/owner checks pass

### Wave 2: devel

- [ ] base devel packages in `devel`
- [ ] no cross-group dependency breakage

### Wave 3: networking/fetch

- [ ] fetch/network tooling packaged and validated
- [ ] `opkg update` over network stable

### Wave 4: desktop prerequisites

- [ ] desktop runtime deps grouped under `desktop`
- [ ] xorg-smoke prerequisites pass

### Wave 5: xorg + xdm

- [ ] Xorg stack installable and launchable
- [ ] XDM path starts and fails safely when needed

### Wave 6: xfce core

- [ ] xfce session essentials installable
- [ ] terminal/file manager/settings baseline works

## Release Gates

- [ ] "Minimum Viable XFCE Boot" definition met
- [ ] "Minimum Viable Daily-Usable Obelisk" definition met
