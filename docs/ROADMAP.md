# Obelisk Roadmap

This roadmap translates the project philosophy into incremental, testable
milestones.

## M0 - Build Integrity (current focus)

- Kernel and userland build via top-level `make`
- Rootfs is packaged reproducibly
- ISO includes kernel and packaged rootfs artifact
- Build docs and release checklist are accurate

## M1 - Boot to PID 1

- Kernel loads and launches `/sbin/init`
- Basic process lifecycle syscalls (`fork`, `execve`, `wait4`, `exit`) operate
- Panic-free boot with repeatable QEMU smoke test

## M2 - AxiomFS Operational Baseline

- Root mount with durable metadata flow
- Scrub/check paths are callable without full fsck dependency
- Policy daemon handoff path is functional end-to-end
- Fail-safe mode when policy daemon is unavailable

## M3 - Sysctl-First Control Plane

- Runtime tuning for AxiomFS and kernel state exposed via sysctl
- Stable userspace `sysctl` tooling for read/write operations
- Compatibility plan for optional procfs (installer-selectable)

## Current Consolidation Gate (post-M3)

- Regression target validates stable boot + core userland workflow
- Command behavior is predictable: either real work or explicit error/usage
- Documented supported command set + known limitations

## Current Major Subsystem In Progress

- Shell pipeline plumbing (`|`) initial bring-up implemented
- Next step: pipeline hardening and parser interaction cleanup
- Keep login/passwd and `/proc` expansion sequenced after pipe hardening

## M4 - Appliance/NAS Features

- Minimal network stack suitable for storage appliance workloads
- First network file protocol target (NFS or SMB)
- Persistent storage auto-discovery and safe mount policy
- Operational docs for always-on deployment

## M5 - Release Candidate Hardening

- Fault-injection test passes for filesystem corruption scenarios
- Soak tests with sustained IO and repeated reboots
- Clear upgrade/migration strategy for on-disk metadata revisions
- Release notes + known issues + rollback guidance
