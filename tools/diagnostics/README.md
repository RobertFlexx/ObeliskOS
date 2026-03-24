# Obelisk Diagnostics

These scripts help triage boot/runtime panics quickly.

## Capture a serial run log

```bash
make diag-capture TIMEOUT=180
```

This writes a timestamped log under `build/diagnostics/`.

## Extract the most relevant panic block

```bash
make diag-extract LOG=build/diagnostics/run-serial-YYYYmmdd-HHMMSS.log
```

## Symbolicate addresses against `kernel.elf`

```bash
make diag-symbolicate LOG=build/diagnostics/run-serial-YYYYmmdd-HHMMSS.log
```

Optional:

```bash
make diag-symbolicate LOG=<log> KERNEL_ELF=build/kernel.elf
```

Use these after a panic to correlate `RIP`/register values with kernel symbols.

## One-shot triage (recommended)

```bash
make diag-triage TIMEOUT=180
```

This runs capture, prints the latest panic block, and prints a symbolication report.

## One-shot repro triage (auto-runs `sudo ls`)

```bash
make diag-triage-repro TIMEOUT=240
```

Optional tuning:

```bash
make diag-triage-repro CMD="sudo ls" BOOT_WAIT=25 POST_WAIT=10 TIMEOUT=240
```

This injects a command into the serial console, captures output, extracts the panic block, and symbolicates addresses automatically.

## Regression validation (consolidation phase)

Run the deterministic smoke validation suite:

```bash
make validate-regression TIMEOUT=200
```

This checks the core "stable minimal UNIX-like" flow:
- boot to shell prompt
- `ls` / `ls -l`
- `touch` / `write` / `cat` / `rm`
- `mkdir` / `rmdir`
- `sysctl -a`
- `head <file>`
- `wc <file>`
- `users`

If any required output is missing (or panic/not-found markers appear), the target fails and prints the saved log path.

## Demo smoke run

For a short non-gating demo script:

```bash
make demo-smoke TIMEOUT=180
```

This captures a guided command sequence log under `build/diagnostics/`.
