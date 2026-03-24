# Obelisk Userland Commands (Current)

This document tracks what is currently supported in the built-in BusyBox-style
userland, and what remains intentionally minimal.

## Supported Commands

- Shell/builtins:
  - `help`, `echo`, `cd`, `pwd`, `clear`, `exit`
  - chaining: `&&`, `||`, `;`
  - minimal pipes: `cmd1 | cmd2` (single-stage pipelines supported)
  - redirects: `<`, `>`, `>>`
  - quoting + `$VAR` expansion
  - history and TAB completion
- Filesystem:
  - `ls`, `ls -l`, `ls -la`
  - `touch`, `cat`, `write`, `rm`, `rmdir`, `mkdir`
  - `chmod` (octal modes), `chown`, `stat`
- Identity:
  - `whoami`, `id`, `users`, `su`, minimal `sudo`
- System:
  - `uname`, `sysctl`, `reboot`, `shutdown`, `halt`, `poweroff`
- Text/minimal coreutils:
  - `head`, `tail`, `wc`, `cut`, `true`, `false`

## Behavior Notes

- Commands are expected to either:
  - perform real work, or
  - return a clear usage/error message.
- Silent no-op behavior is treated as a bug.
- `head`/`tail`/`wc`/`cut` can read from stdin when no files are provided.
- `head -h`, `tail -h`, `wc -h`, and `cut -h` print usage.

## Known Limitations (Intentional for this stage)

- Shell:
  - pipelines currently do not support inline redirects in pipeline segments
  - complex multi-feature parser combinations are still intentionally minimal
  - no job control
  - no full POSIX shell scripting support
- `chmod`:
  - octal modes only (symbolic modes like `+x` are rejected with a clear error)
- `users`:
  - minimal implementation (current user identity view)
- `/proc`:
  - compatibility mount surface is minimal and not yet feature-complete

## Next Major Subsystem (single focus)

After initial pipe bring-up, the next subsystem priority is:

1. real login/passwd flow
2. `/proc` process/introspection expansion
