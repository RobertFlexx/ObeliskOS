# Packaging Policy (v1)

## Packaging priorities

1. reliability and inspectability
2. predictable installs/removals
3. clear metadata and repository hygiene

## v1 package database layout

```text
/var/lib/opkg/
  installed/
    <pkg>.json
  fileowners.json
  repos/
    <repo>.json
  cache/
    <pkg-file>.opk
```

Supports:

- installed package list
- installed file manifests
- file ownership lookup
- version tracking

### Installed record shape (`installed/<pkg>.json`)

```json
{
  "meta": {
    "name": "hello-sample",
    "version": "1.0.0",
    "arch": "x86_64",
    "summary": "...",
    "description": "...",
    "depends": [],
    "provides": [],
    "conflicts": [],
    "maintainer": "...",
    "section": "core"
  },
  "installed_files": [
    "/usr/share/doc/hello-sample.txt"
  ]
}
```

### File owner map (`fileowners.json`)

```json
{
  "owners": {
    "/usr/share/doc/hello-sample.txt": "hello-sample"
  }
}
```

Removal safety rule in v1:

- `opkg remove <pkg>` aborts if any manifest file is mapped to a different owner.

## Build/install model recommendation

**Recommended direction: hybrid, phased as binary-first.**

- Near-term (v1/v1.1): binary packages are primary (practical bootstrap path).
- Mid-term: add source recipe metadata and build tooling.
- Long-term: hybrid repositories with binary packages + source recipe flow.

Why this is the practical path now:

- Obelisk needs reliable reproducible installs first.
- Binary-first minimizes bootstrap complexity.
- A hybrid target preserves long-term UNIX-style ports flexibility.

## Grouping policy for repositories

- `core` (base system)
- `devel` (toolchains/dev tools)
- `extra` (optional utilities)
- `desktop` (generic desktop dependencies)
- `xfce` (official default desktop stack)
