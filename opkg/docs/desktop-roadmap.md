# Desktop Roadmap (XFCE-first)

XFCE is the official and default desktop target for Obelisk.

## Policy

- XFCE is priority #1 for desktop packaging, QA, and integration.
- Other DEs are explicitly not first-priority in this phase.
- Desktop repo grouping and validation focuses on XFCE compatibility first.

## Required OS prerequisites before realistic XFCE bring-up

- display/server architecture decision (X11-first recommended for XFCE path)
- graphics/display stack and device support
- input/event stack (keyboard/mouse integration)
- PTY/session model suitable for graphical login/session startup
- userspace library baseline needed by GTK/XFCE

## Required package groups (planned)

- `core`: base runtime and shell system
- `devel`: compilers/build/debug tooling
- `extra`: optional utilities
- `desktop`: generic desktop prerequisites
- `xfce`: XFCE session/components

## XFCE-focused package categories

- X server and core X11 plumbing
- GTK stack
- session manager and settings daemon
- terminal emulator
- file manager
- panel/window manager/session components
- fonts/themes/icons baseline
- display/login manager choice (lightdm-style class)

## Suggested sequencing

1. shell-only stable base + package ecosystem (current)
2. X11/graphics/input prerequisites
3. GTK runtime baseline
4. XFCE core session path
5. desktop QA and default profile

## Immediate compatibility work to continue XFCE direction

### OS/runtime prerequisites (next)

- choose display stack contract (X11-first for XFCE v1 target)
- define input/event API boundary for keyboard/mouse devices
- formalize multi-user session and PTY model for graphical login
- define shared library/runtime baseline expected by GTK userspace

### Packaging prerequisites (next)

- establish desktop-focused package set in `desktop` and `xfce` groups
- define dependency baselines for GTK/X11 runtime components
- publish compatibility matrix for:
  - core desktop libraries
  - session manager pieces
  - file manager + terminal stack

### QA path for graphical application compatibility

- smoke-boot into graphical session target
- launch baseline GUI apps (terminal, file manager, settings)
- verify session logout/restart reliability
- verify package install/remove correctness for GUI stack components
