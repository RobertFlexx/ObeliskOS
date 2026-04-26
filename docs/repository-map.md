# ObeliskOS Repository Map

# "where do i put this code" guide for newcomers.

---

Welcome! if you just landed here and you are trying to figure out where code lives, this is the map.

This doc is meant to answer two fast questions:

- where is subsystem X implemented?
- where should new feature Y go?

If you are new to the project, pair this with `README.md` and `docs/architecture.md`.

---

## Quick Top-Level Map

- `kernel/` - monolithic kernel sources (boot, memory, process, fs, ipc, net, sysctl)
- `userland/` - libc + init stack + CLI tools + desktop helper programs
- `opkg/` - native Obelisk `opkg` tool, package format/docs, and package examples
- `docs/` - architecture notes, roadmaps, ABI/protocol docs, release policy
- `tools/` - build helpers, diagnostics, repo tooling, import scripts
- `rootfs-overlay/` - files copied into rootfs image during `make rootfs`
- `Makefile` - top-level orchestration (kernel/userland/rootfs/iso/qemu/diagnostics)

---

## Kernel Map (`kernel/`)

- `arch/x86_64/` - architecture-specific code
  - `boot/` early bootstrap, GDT/IDT, interrupt entry assembly
  - `io/` low-level ports/UART
  - `mm/` arch paging + physical memory helpers
  - `process/syscall.c` syscall entry/dispatch table and syscall handlers
- `kernel/` - generic core services (`main.c`, panic, printk, time, bootinfo, initramfs)
- `mm/` - PMM/VMM/kmalloc
- `proc/` - task/process state, scheduler, fork/exec
- `fs/` - VFS and filesystem implementations
  - `axiomfs/` AxiomFS implementation
  - `devfs/` device filesystem
  - `include/` FS-private headers
- `ipc/` - message queues, shared memory, axiomd channel glue
- `net/` - network stack + NIC/Wi-Fi drivers (`e1000`, `r8169`, `virtio_net`, `wifi_*`)
- `drivers/` - non-network hardware drivers (currently PCI base pieces)
- `sysctl/` - sysctl core, nodes, and handlers
- `lib/` - freestanding kernel support utils (`string`, `printf`, `bitmap`, `rbtree`)
- `include/` - kernel headers
  - `uapi/` headers shared with userland ABI surface

Build wiring reminder: new kernel `.c` or `.zig` files must be added to `kernel/Makefile` (`C_SOURCES` / `ZIG_SOURCES`).

---

## Userland Map (`userland/`)

- `libc/` - tiny libc + syscall wrappers
- `init/` - legacy init target
- `initd/` - `obeliskd` userspace init/session manager path
- `axiomd/` - policy/prolog assets for system policy runtime
- `bin/` - CLI and desktop-side user programs (`sysctl`, `rockbox`, `opkg`, probes, account tools, etc)
- `lib/` - shared userland helper code reused by multiple binaries
- `Makefile` - userland build graph and program list (`PROGRAMS`)

Build wiring reminder: new user programs usually need:

1. source directory under `userland/bin/<toolname>/`
2. a target/link rule in `userland/Makefile`
3. inclusion in `PROGRAMS` so it builds by default
4. optional rootfs copy coverage in top-level `Makefile` loops if you want it in the image by default

---

## Packaging + Runtime Content

- `opkg/` - package manager implementation + package docs/examples
- `opkg/examples/` - reference package layouts and runtime bundles
- `obelisk-repo/` - repository-side workspace for package distribution flow
- `rootfs-overlay/` - static files merged into built image (`make rootfs`)

Use `rootfs-overlay/` for config defaults and base image content that is not compiled code.

---

## Where New Features Should Go

### New hardware driver

- network NIC/Wi-Fi: `kernel/net/`
- other buses/devices: `kernel/drivers/`
- shared driver ABI/header: `kernel/include/drivers/`
- if user-visible ABI is needed, add constants/structs to `kernel/include/uapi/`
- register/build it via `kernel/Makefile`

### New filesystem

- new FS implementation: `kernel/fs/<fsname>/`
- common VFS integration: `kernel/fs/vfs.c` and FS-facing headers in `kernel/fs/include/`
- if mount flags/ioctls cross userland boundary, define UAPI in `kernel/include/uapi/`
- include sources in `kernel/Makefile`

### New syscall

- syscall number/API: `kernel/include/uapi/syscall.h`
- implementation + table hookup: `kernel/arch/x86_64/process/syscall.c`
- userland wrapper glue: `userland/libc/` (usually `syscall.c` plus prototype headers)
- add a probe/test command under `userland/bin/` when possible

### New process scheduler or proc feature

- primary code: `kernel/proc/`
- arch handoff/context interfaces: `kernel/arch/x86_64/process/`

### New memory-management feature

- generic alloc/vm logic: `kernel/mm/`
- arch paging bits: `kernel/arch/x86_64/mm/`

### New IPC mechanism or axiomd integration

- kernel IPC endpoint/primitive: `kernel/ipc/`
- policy protocol docs: `docs/ipc-protocol.md`
- userspace daemon/policy interaction: `userland/axiomd/` and/or `userland/initd/`

### New userland command

- command source: `userland/bin/<name>/`
- reusable support code: `userland/lib/`
- libc/API additions: `userland/libc/`
- wire into build in `userland/Makefile`
- include in rootfs loops in top-level `Makefile` if it should ship in default image

### New sysctl node/control

- kernel node/handler: `kernel/sysctl/nodes.c` and `kernel/sysctl/handlers.c`
- core sysctl behavior: `kernel/sysctl/sysctl.c`
- reference docs update: `docs/sysctl-reference.md`

### New docs/spec/roadmap item

- put it in `docs/`
- if it changes contributor flow, link it from `README.md`

---

## Fast Contribution Checklist

- Add code in the subsystem directory (map above).
- Wire the file into the right `Makefile`.
- If ABI changed, update `kernel/include/uapi/` and userland callers.
- If behavior is user-facing, add/update command docs in `docs/`.
- If image contents changed, verify top-level `Makefile` rootfs packaging paths.

---

## Common "oops" moments

- code compiles locally but not in CI/image: file was not added to `kernel/Makefile` or `userland/Makefile`
- command builds but is missing at runtime: it was not copied during `make rootfs`
- syscall works in kernel but not userland: missing `uapi` number/prototype/wrapper sync
- feature exists but newcomers cannot find it: docs not updated in `docs/`

---

From Axioms, Order.

