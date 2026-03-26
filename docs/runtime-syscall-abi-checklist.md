# Runtime Syscall ABI Checklist (Desktop startup)

## Goal
Make real `ld-linux` / `ld.so` startup for desktop binaries proceed far enough that `xinit` / `xdm` can fully initialize, one smallest kernel ABI gap at a time.

## Implemented in previous pass
These syscalls were observed as the earliest `ld.so` startup blockers and are now registered.

- `218` `set_tid_address`: implemented (returns `0` on success; writes user `tidptr` when non-null)
- `273` `set_robust_list`: implemented as a conservative single-thread stub (`return 0`)
- `228` `clock_gettime`: implemented (minimal timestamp based on `get_ticks()`)
- `302` `prlimit64`: implemented (fills `old_limit` with conservative stack limit; `return 0`)
- `318` `getrandom`: implemented as deterministic pseudo-random bytes; `return buflen`
- `334` `rseq`: implemented as minimal “compat exists” stub (`return 0`, no real rseq area support)
- `157` `prctl`: implemented minimal subset for `PR_SET_NAME` / `PR_GET_NAME`; other codes return `-ENOSYS`

## Implemented in this pass
These were the next runtime blockers after the RSVD/NX page-table fix:

- `13` `rt_sigaction`: implemented basic Linux-compatible `rt_sigaction` handling (`sa_handler`, `sa_flags`, `sa_restorer`, mask)
- `14` `rt_sigprocmask`: implemented basic block/unblock/setmask semantics
- `56` `clone`: implemented conservative process-style clone via `do_fork`; thread-style clone flags (`CLONE_THREAD`, `CLONE_SETTLS`, child tid clear/set) currently return `-ENOSYS`
- `95` `umask`: implemented UNIX semantics (`returns old umask`, stores masked `0777`)
- `137` `statfs`: implemented path-based statfs with basic superblock-backed values

## Validation notes (current state)
- `execprobe /usr/bin/xinit`
  - no longer reports missing `13/14/56`
  - reaches `/usr/bin/xinit: server error` (deeper userland stage)
- `execprobe /usr/bin/xdm`
  - no longer reports missing `95/137`
  - reaches policy gate: `Only root wants to run /usr/bin/xdm`

## Intentional stubs / approximations
- `set_robust_list`, `rseq`, `getrandom`, `prlimit64` remain best-effort approximations from prior pass.
- `clone` currently supports process-style usage; thread-oriented clone/TLS setup is intentionally not faked.

## Next smallest blocker
- For `xinit`: identify concrete userland/server failure reason after syscall ABI is present (`server error` path).
- For `xdm`: current gate is policy/privilege (`Only root wants to run /usr/bin/xdm`), not missing syscall ABI.

