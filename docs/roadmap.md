# Obelisk Roadmap (XFCE Path)

This roadmap defines the shortest sane path from current CLI-capable Obelisk
to a daily-usable minimal Unix-like system with one official desktop path:
XFCE on XDM.

## Target System Definition

- CLI-first base system, desktop-capable
- official desktop environment: XFCE
- official display/login manager: XDM
- official package manager and ecosystem: opkg + binary `.opk` repositories
- TTY remains the safe fallback when desktop boot/login fails

## Phase A - Solid Base OS

### Subsystem Goals

- Stable boot and init service graph
- Stable shell and core userland command behavior
- Stable filesystem and permission semantics
- Stable networking (DHCP/static), DNS, and basic socket reliability
- Stable package-manager core path (`update`, `install`, `remove`)
- Documented static vs dynamic executable policy

### Likely Blockers

- Boot/session regressions when service startup fails
- Inconsistent exec behavior between ELF and script paths
- Core tools missing behavior parity needed for installer/session scripts

### Likely Modules/Files

- `kernel/proc/*`, `kernel/fs/*`, `kernel/net/*`
- `userland/init/*`, `userland/initd/*`, `userland/bin/rockbox/*`
- `userland/bin/installer*/*`
- `userland/bin/opkg/*`, `opkg/examples/*`

### Package Groups In Scope

- `core`

### Validation Checklist

- Boot to interactive shell across repeated cold boots
- `fork/execve/wait/exit` stable for shell scripts and binaries
- DHCP + DNS + fetch smoke pass
- `opkg update/install/remove` pass on core package set
- Explicit documented behavior for unsupported commands/features

### Done Means

- Base OS can be used from TTY daily without panic loops
- Package install/update/remove is reliable for core packages

## Phase B - Packaging and Software Ecosystem

### Subsystem Goals

- Fully usable `opkg update/search/install/remove/info/files/owner`
- Network-backed binary `.opk` repository flow is practical
- Group taxonomy locked: `core`, `devel`, `extra`, `desktop`, `xfce`
- Common utility packages are installable and removable cleanly

### Likely Blockers

- Incomplete package metadata consistency
- Owner/file manifest drift during upgrade/remove
- Weak failure recovery in interrupted installs

### Likely Modules/Files

- `userland/bin/opkg/*`
- `opkg/docs/*`, `opkg/examples/*`, `opkg/repo-site/*`
- host-side index/repo publish scripts under `tools/`

### Package Groups In Scope

- `core`, `devel`, `extra`

### Validation Checklist

- Repo bootstrap from clean image to updated index
- Install/remove cycles keep ownership database consistent
- `opkg search/info/files/owner` output matches installed manifests
- Basic dependency and conflict checks behave deterministically

### Done Means

- opkg is operational backbone for non-desktop and desktop installs

## Phase C - Desktop Runtime Prerequisites

### Subsystem Goals

- Dynamic ELF and shared-library execution stable under desktop workloads
- PTY/session/process control mature enough for login/session chains
- Poll/select + event-loop readiness
- Practical local IPC support required by XDM/XFCE stack
- libc/runtime compatibility baseline documented and testable

### Likely Blockers

- Dynamic loader ABI gaps (auxv, TLS, relocation/runtime assumptions)
- Missing AF_UNIX, poll/select, futex/threading behavior used by real binaries
- Session and signal semantics mismatches for DM/session daemons

### Likely Modules/Files

- `kernel/proc/exec.c`, `kernel/arch/x86_64/process/syscall.c`
- `kernel/net/*` (AF_UNIX if needed), scheduler/signal/tty subsystems
- `userland/libc/*`
- runtime probes under `userland/bin/*probe*`

### Package Groups In Scope

- `desktop` prereq runtime set

### Validation Checklist

- Real dynamic binaries (`xinit`, `xdm`, `xfce4-session`) execute without fatal ABI faults
- Session launch flow survives start/stop loops
- IPC/event-loop smoke tests pass

### Done Means

- Runtime can host real desktop daemons, not only wrapper scripts

## Phase D - Graphics/Input/Windowing Substrate

### Subsystem Goals

- Stable framebuffer output path and VT behavior
- Stable keyboard/mouse event delivery under GUI load
- X11-first substrate practical for XFCE target
- Font/text rendering prerequisites packaged and working

### Likely Blockers

- Input queue starvation/edge-key handling regressions
- Display init and VT transitions causing session drops
- Incomplete graphics runtime deps

### Likely Modules/Files

- `kernel/fs/devfs/*`, input and framebuffer drivers
- `userland/bin/fbinfo/*`, `userland/bin/xorg-smoke/*`, `desktop-session`
- `opkg/examples/xorg/*`, `opkg/examples/desktop-base/*`

### Package Groups In Scope

- `desktop`

### Validation Checklist

- `xorg-smoke` pass before and during desktop launch attempts
- No input lockups in TTY and X session transitions
- X server starts with expected display/input devices

### Done Means

- Graphical substrate is stable enough for XDM-managed sessions

## Phase E - XDM Login Path

### Subsystem Goals

- `desktop_mode=xdm` supported as official graphical boot path
- Installer profile selection supports desktop mode wiring
- Automatic fallback to TTY if XDM/session launch fails
- Failure counters/recovery policy avoids boot loops

### Likely Blockers

- Init/autostart recursion on failed display manager startup
- Session wrapper assumptions exceeding shell/runtime capabilities

### Likely Modules/Files

- `userland/initd/*`, `userland/init/*`
- `userland/bin/desktop-session/*`
- `opkg/examples/xdm/*`, XDM compat config/package scaffolding
- installer docs and profile logic

### Package Groups In Scope

- `desktop`, `xfce`

### Validation Checklist

- `desktop_mode=xdm` boots to login/session attempt path
- Failure returns to TTY with usable shell and clear diagnostics
- Reboot stability after repeated failed/successful desktop attempts

### Done Means

- XDM path is official, recoverable, and operationally safe

## Phase F - XFCE Package Wave

### Subsystem Goals

- Package and integrate XFCE core with minimum required runtime
- Ensure required support libraries and data files are included
- Keep package set minimal and coherent for official desktop

### Likely Blockers

- Missing shared libs or runtime layout assumptions
- Session components failing due to partial package splits

### Likely Modules/Files

- `opkg/examples/xorg/*`, `opkg/examples/xdm/*`, `opkg/examples/xfce/*`
- package import/build scripts under `tools/desktop/*`
- repo index and metadata generation

### Package Groups In Scope

- `xfce`

### Validation Checklist

- `opkg install-profile xorg xfce xdm` succeeds from clean image
- XFCE session components launch from XDM flow
- Terminal, panel, file manager, settings components present

### Done Means

- First complete official XFCE package wave works end-to-end

## Phase G - Daily-Usable Polish

### Subsystem Goals

- Desktop login works reliably
- TTY fallback always usable
- Networking/package management functional in desktop installs
- Session startup/logout/restart behavior is coherent

### Likely Blockers

- Intermittent session startup races
- Runtime regressions from package updates

### Likely Modules/Files

- all desktop runtime touchpoints from phases C-F
- validation scripts and release checklists

### Package Groups In Scope

- `core`, `desktop`, `xfce`

### Validation Checklist

- desktop login and TTY fallback pass reboot loop tests
- terminal + file manager + network tools usable in GUI session
- package manager usable from desktop install and TTY fallback

### Done Means

- "Daily-usable minimal Obelisk" quality bar is met

## Recommended Implementation Order (Now -> First Real XFCE Boot)

1. Stabilize dynamic-loader/runtime ABI so real `xinit/xdm/xfce4-session` stop faulting in loader startup.
2. Close remaining runtime ABI gaps (signals/futex/threading edge cases) discovered by real binary traces.
3. Lock package wave integrity for `xorg` + `xdm` + `xfce` profiles.
4. Validate full login/session/logout loops with failure recovery.
5. Ship first "official XFCE boot" milestone and freeze regression tests.

## Milestone Status: XDM Safe Boot + TTY Recovery

Implemented baseline:

- one-shot desktop auto-start attempt in `desktop_mode=xdm`
- guaranteed fallback to interactive TTY shell on failure
- persistent boot-state tracking in `/var/lib/obelisk/boot-state.json`
- consecutive failure threshold (3) to suppress auto-XDM attempts
- manual recovery override with `/etc/obelisk/force-tty`

Follow-up focus remains runtime compatibility for real XDM/XFCE binaries.

## Milestone Status: Runtime ABI Hardening (Current Pass)

Implemented in this pass:

- Added AF_UNIX stream runtime support across `socket/bind/listen/connect/accept/sendto/recvfrom/socketpair`.
- Added `poll`, `select`, and `nanosleep` syscalls for desktop/event-loop behavior.
- Added process-group/session syscalls (`setpgid`, `getpgrp`, `setsid`, `getpgid`, `getsid`) and process state fields (`pgid`, `sid`).
- Updated close/shutdown semantics so AF_UNIX peers observe closure instead of dangling connected state.
- Lowered socket fd base from `4096` to `512` so `select`/fdset-based code can operate on socket fds.
- Added `runtime-abi-probe` userland regression probe and wired it into rootfs packaging.

Validated:

- `runtime-abi-probe` passes for AF_UNIX socketpair data path, `poll`, `select`, and session syscalls.
- Safe-boot behavior remains unchanged (one-shot XDM attempt, fallback, suppression, recovery sentinels).

Still blocking first real XFCE boot:

- Real `xinit` and `xdm` binaries now execute far enough to enter dynamic loader but fault very early in loader startup (`RIP` in `ld-linux` region, user page fault on null+8 access).
- Targeted diagnostics show the crash is inside dynamic-loader exception handling (`_dl_catch_error` control flow): the faulting instruction loads from `0x68(%rdi)` and then dereferences `0x8(%rdx)` where `rdx==0`.
- This indicates remaining dynamic loader/runtime ABI incompatibility (post-exec startup path), now the top-priority blocker.

## Milestone Status: Loader-Focused Runtime ABI Hardening (Current Pass)

Implemented in this pass:

- Added targeted exec/loader instrumentation in `kernel/proc/exec.c` for:
  - main/interpreter base/entry/phdr metadata
  - PT_DYNAMIC inspection (mapped memory vs source file first entry)
  - final stack pointer alignment and user transition point
  - expanded auxv tracing for `xinit`/`xdm`/`ld-linux` targets
- Added `AT_HWCAP`/`AT_HWCAP2` baseline values for x86_64 startup compatibility.
- Added tiny `loader-elf-probe` userland diagnostic (`userland/bin/loader-elf-probe`) to verify on-disk ELF PT_DYNAMIC state in-system.

Validated:

- Minimal dynamic startup now works: `/lib64/ld-linux-x86-64.so.2 --help` runs and exits cleanly.
- On-disk ELF dynamic sections for `/usr/bin/xinit` and `/usr/bin/xdm` are correct (`first_tag=DT_NEEDED`).
- During `execve`, mapped main-image PT_DYNAMIC for these binaries is observed as zeroed (`DT_NULL` first entry), while interpreter PT_DYNAMIC is valid.

Current highest-confidence blocker:

- Main ET_DYN image mapping/copy path remains the primary suspect for corrupting the loader’s runtime internal state (PT_DYNAMIC mapping/copy mismatch was observed), with the current concrete failure site now pinned to `ld-linux` dynamic-loader exception handling (`_dl_catch_error` deref via `0x68(%rdi)`).

Next immediate milestone:

- Fix ET_DYN main-image segment mapping/copy correctness so PT_DYNAMIC in memory matches file contents, then re-validate `xinit`/`xdm` startup depth.
