# Desktop Roadmap (Official XFCE + XDM Path)

This document defines the only supported desktop direction for current Obelisk
milestones: XFCE sessions launched via XDM.

## Official Desktop Policy

- Official desktop environment: **XFCE**
- Official display/login manager: **XDM**
- Official graphical boot mode: `desktop_mode=xdm`
- Official recovery path: **fallback to TTY shell**
- Non-goal for this phase: KDE/GNOME parity

## Practical End-State

- System boots to XDM when configured for desktop mode
- User can log into XFCE session
- If XDM/session fails, system falls back to TTY with diagnostics
- opkg remains the install/update path for desktop components

## Supported Launch Flows

1. Installer selects desktop profile and sets `desktop_mode=xdm`.
2. Init service graph starts desktop launch path.
3. `obeliskd` attempts `desktop-session xdm` once per boot.
4. If launch fails, boot falls back to TTY shell immediately.
5. Repeated failures trigger temporary XDM auto-start suppression.
6. Manual override file can force TTY boot regardless of configured mode.

## Subsystem Plan

### 1) Boot and Service Integration

Goals:

- `desktop_mode=tty` stays default-safe
- `desktop_mode=xdm` starts official graphical path
- failure does not create init restart storms

Likely files:

- `userland/initd/*`
- `userland/init/*`
- installer profile wiring in `userland/bin/installer*/*`

Done means:

- predictable startup behavior for both tty and xdm modes

### 2) Session Launcher Contract (`desktop-session`)

Goals:

- single entrypoint for graphics validation and launch policy
- clear exit codes and log messages
- deterministic fallback behavior when launcher/runtime fails

Likely files:

- `userland/bin/desktop-session/desktop_session.c`
- `userland/bin/xorg-smoke/*`

Done means:

- launcher behavior is testable and scriptable in CI/repro runs

### 3) XDM Integration

Goals:

- XDM config and runtime packaged in official profile path
- no dependence on brittle multi-layer script wrappers for core path
- practical DM startup and recovery behavior

Likely files:

- `opkg/examples/xdm/*`
- `opkg/examples/xorg/*`
- desktop compatibility wrappers as temporary compatibility shims only

Done means:

- XDM is bootable and recoverable across repeated test cycles

### 4) XFCE Session Integration

Goals:

- include only essential XFCE components first
- ensure session, panel, settings, terminal, file manager work
- keep package scope minimal and maintainable

Likely files:

- `opkg/examples/xfce/*`
- package import tooling under `tools/desktop/*`

Done means:

- official XFCE session boots and remains usable

## Runtime Compatibility Checklist Before "Real XFCE"

- dynamic ELF + loader ABI stable for `xinit`, `xdm`, and XFCE binaries
- TLS/`arch_prctl` behavior stable under multi-process desktop workload
- PTY/session semantics sufficient for terminal and login behavior
- signal/process control behavior robust for session daemons
- local IPC expectations satisfied (AF_UNIX where required)
- `poll`/`select` event paths usable for desktop daemons
- libc/runtime environment and filesystem layout expectations met

### Current Runtime ABI Status

Working now:

- AF_UNIX local socket path for stream IPC (`socketpair`, bind/listen/connect/accept/send/recv).
- `poll`/`select` readiness for socket/event loops.
- `setsid`/`setpgid`/`getsid`/`getpgid` process-session control needed by launcher chains.
- `nanosleep` baseline timing syscall.
- Dedicated `runtime-abi-probe` tool to regress these prerequisites.

Still failing:

- Real dynamic desktop binaries (`xinit`, `xdm`) reach loader startup and then fault early in dynamic-loader runtime.
- Remaining work is now focused on dynamic loader/ELF runtime startup correctness, not basic IPC/poll/session syscall absence.

### Loader-Focused Findings (Newest)

- `ld-linux` standalone startup now succeeds (`/lib64/ld-linux-x86-64.so.2 --help`), confirming baseline loader runtime can execute.
- `xinit`/`xdm` still fault early in loader startup.
- Instrumentation now shows a concrete mismatch:
  - on-disk ELF PT_DYNAMIC for `xinit`/`xdm` is valid
  - mapped main-image PT_DYNAMIC at exec time is zeroed (`DT_NULL` first tag)
  - interpreter PT_DYNAMIC remains valid
- This isolates current blocker to main executable ELF mapping/copy correctness, not missing AF_UNIX/poll/session/syscall primitives.

## Validation Gates

## Gate D1 - Desktop Prereq Smoke

- `xorg-smoke` pass
- input events and VT checks pass
- no kernel panic during launch attempts

## Gate D2 - XDM Launch Safety

- `desktop-session xdm` reaches launch attempt deterministically
- launch failure returns to TTY with useful diagnostics
- no infinite restart loops

## Gate D3 - XFCE Session Basic Usability

- terminal opens
- file manager opens
- settings tool opens
- networking and opkg commands work inside session terminal

## Gate D4 - Reboot and Recovery

- repeated boot cycles in `desktop_mode=xdm` remain stable
- forced XDM failure still leaves a usable TTY

## Installer and Boot Policy Scaffolding

- Installer should expose desktop mode choice with explicit note:
  - `tty` = safest base mode
  - `xdm` = official graphical mode
- Desktop profile installation should ensure required packages:
  - `xorg`, `xdm`, `xfce` (and dependencies)
- Boot-time launch path should:
  - check installed profile markers
  - run `desktop-session xdm`
  - on failure, mark attempt and drop to TTY

## Implemented Safe-Boot Behavior

Current behavior in `obeliskd`:

- Reads configured desktop mode (`/etc/obelisk-desktop.conf` or installer config fallback).
- `desktop_mode=tty`: launches interactive shell directly.
- `desktop_mode=xdm`:
  - attempts `/bin/desktop-session xdm` once
  - on non-zero/exec failure: logs and falls back to shell
  - tracks failures in `/var/lib/obelisk/boot-state.json`
  - suppresses future auto-XDM launches after 3 consecutive failures
- Manual forced recovery:
  - if `/etc/obelisk/force-tty` exists, skips desktop launch and boots TTY
  - suppression can be explicitly reset with `/etc/obelisk/re-enable-xdm`

Diagnostic output/logging:

- text log file: `/var/log/desktop-session.log`
- includes launch attempts, failure status, suppression decisions, and overrides

## Immediate Milestone

**Milestone: "Dynamic Loader Startup Stability for Real XDM/XFCE Binaries"**

Scope:

- fix main ET_DYN mapping/copy so PT_DYNAMIC in memory matches on-disk content
- validate `xinit` and `xdm` pass early loader startup and reach deeper runtime init
- keep existing safe-boot fallback/recovery policy unchanged
