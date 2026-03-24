# Repository Format (v1)

`opkg` v1 uses a simple static HTTP repository model.

## Layout

```text
repo/
  index.json
  packages/
    foo-1.0-x86_64.opk
    bar-2.1-x86_64.opk
```

## `index.json`

Top-level array of package entries.

Each entry includes:

- `name`
- `version`
- `arch`
- `filename`
- `checksum`
- `depends` (array)
- `summary`

`filename` is relative to `packages/`.
`checksum` is required in v1 and must be `sha256:<64-lower-hex>`.

Index generation helper:

- `opkg repo index <repo-dir>`
  - scans `<repo-dir>/packages/*.opk`
  - extracts metadata from each package
  - computes `sha256` for each package
  - writes `<repo-dir>/index.json`

## Local repo config

`/etc/opkg/repos.conf` (v1 proposal):

```text
main https://packages.obelisk.org/main
desktop https://packages.obelisk.org/desktop
```

Format is intentionally simple:

- `<name> <base-url>`
- one repo per line
- comments begin with `#`

## Local cached state

Repo state is stored under:

`/var/lib/opkg/repos/*.json`

Downloaded package cache:

`/var/lib/opkg/cache/*.opk`

## Implemented v1 command behavior

- `opkg update`:
  - reads repo definitions from `repos.conf`
  - fetches `<base-url>/index.json` for each repo
  - validates each downloaded index is a JSON array
  - writes cached copy to `/var/lib/opkg/repos/<repo>.json`
- `opkg search <term>`:
  - searches cached indexes (run `opkg update` first)
  - matches against package `name` and `summary`
  - prints `repo: name version arch - summary`
- `opkg install <pkg>`:
  - uses cached repo indexes (run `opkg update` first)
  - requires exact package-name match
  - requires architecture match
  - selects highest version if multiple matches exist
  - downloads `<base-url>/packages/<filename>`
  - verifies `sha256` checksum from index
  - installs via the same local `.opk` install path

## Repository management commands

- `opkg repo`
  - lists configured repositories from `repos.conf`
- `opkg repo init <dir>`
  - creates a static repo skeleton (`packages/`, `index.json`, `README.txt`)
- `opkg repo index <dir>`
  - rebuilds `index.json` from package files in `packages/`
