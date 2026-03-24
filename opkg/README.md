# opkg

`opkg` is Obelisk's native package manager and `.opk` package ecosystem entrypoint.

## Goals (v1)

- simple, inspectable package format (`.opk`)
- simple static HTTP repository layout
- simple local JSON package database
- predictable CLI behavior with clear errors
- no overengineered dependency solving in v1

## Scope (v1)

- `opkg update`
- `opkg search <name>`
- `opkg install <pkg|file.opk>`
- `opkg remove <pkg>`
- `opkg list`
- `opkg info <pkg>`
- `opkg files <pkg>`
- `opkg owner <path>`
- `opkg repo`
- `opkg build <dir> [out.opk]`

## Working behavior (implemented)

- install from local `.opk` file (metadata + payload extraction)
- install by package name from cached repos (exact match, arch match, latest version)
- checksum verification (`sha256:<hex>`) for repo-downloaded packages
- remove package by installed manifest
- list/info/files/owner queries from local DB
- repo `update` downloads each configured repo `index.json` into cache
- repo `search` matches package name + summary from cached indexes
- repo helpers:
  - `opkg repo init <dir>` to create static repo skeleton
  - `opkg repo index <dir>` to rebuild checksummed `index.json`

## On-disk state (v1)

```text
/var/lib/opkg/
  installed/
    <pkg>.json
  fileowners.json
  repos/
    <repo>.json
  cache/
    <downloaded-package>.opk
```

## Runtime path overrides

- `OPKG_ROOT` (target filesystem root for payload extraction/removal)
- `OPKG_DB_ROOT` (package database root; default is `$OPKG_ROOT/var/lib/opkg`)
- `OPKG_REPOS_CONF` (repo config path; default is `$OPKG_ROOT/etc/opkg/repos.conf`)

## Build

```bash
cd opkg
dub build
```

## Quick local smoke flow

```bash
cd opkg
./opkg build examples/samplepkg examples/packages/hello-sample-1.0.0-x86_64.opk
./opkg build examples/samplepkg-extra examples/packages/hello-extra-1.1.0-x86_64.opk
OPKG_ROOT=/tmp/opkg-root OPKG_DB_ROOT=/tmp/opkg-root/var/lib/opkg \
  ./opkg install examples/packages/hello-sample-1.0.0-x86_64.opk
OPKG_ROOT=/tmp/opkg-root OPKG_DB_ROOT=/tmp/opkg-root/var/lib/opkg ./opkg list
OPKG_ROOT=/tmp/opkg-root OPKG_DB_ROOT=/tmp/opkg-root/var/lib/opkg ./opkg remove hello-sample
```

## HTTP/web install proof flow

Run:

```bash
./opkg/tools/demo-web-install.sh
```

This script:

- builds a sample package
- generates `index.json`
- serves a static HTTP repo locally
- runs `opkg update`, `opkg search`, `opkg install` over HTTP
- verifies installed payload on disk

## Build common-tool web repo packages

To populate `opkg/repo-site` with installable package examples:

```bash
./opkg/tools/build-common-web-repo.sh
```

This currently builds packages for:

- `hello-sample`
- `grep` (wrapper package)
- `sed` (wrapper package)

Notes:

- `nano` and `gcc` remain pending native ports/toolchain work for Obelisk runtime.
- packages in `opkg/examples/` are intended as practical bring-up targets while
  broader compatibility evolves.

## Repo-backed install flow (v1)

1. configure `/etc/opkg/repos.conf`
2. run `opkg update` to cache repo indexes
3. run `opkg install <pkg>`

Install-by-name behavior:

- exact name match
- architecture must match (`OPKG_ARCH`, default `x86_64`)
- if multiple versions exist, highest numeric dotted version is selected
- downloaded package is cached under `<db-root>/cache/`
- download checksum must match `index.json` checksum field

## Documentation

- `docs/opk-format.md`
- `docs/repo-format.md`
- `docs/github-pages.md`
- `docs/common-tools-status.md`
- `docs/roadmap.md`
- `docs/desktop-roadmap.md`
- `docs/packaging-policy.md`
- `docs/ecosystem-plan.md`
