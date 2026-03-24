# GitHub Pages Repository Hosting

This guide publishes an `opkg` static repository via GitHub Pages.

## 1) Repository layout

Use `opkg/repo-site/` as the published root:

```text
opkg/repo-site/
  index.json
  packages/
    *.opk
```

`index.json` must be regenerated whenever package files change.

## 2) Build/update package index

From project root:

```bash
./opkg/tools/build-common-web-repo.sh
# or manually:
./opkg/opkg repo index opkg/repo-site
```

This scans `opkg/repo-site/packages/*.opk` and writes checksums + metadata to
`opkg/repo-site/index.json`.

## 3) Enable GitHub Pages workflow

This repository includes:

- `.github/workflows/opkg-pages.yml`

The workflow publishes `opkg/repo-site/` to Pages on push to `main` (or manual
dispatch).

## 4) Configure client systems

On clients:

```text
# /etc/opkg/repos.conf
main https://<user>.github.io/<repo>
```

Then:

```bash
opkg update
opkg search <name>
opkg install <pkg>
```

## 5) Release checklist

- copy/build new `.opk` files into `opkg/repo-site/packages/`
- run `./opkg/tools/build-common-web-repo.sh` or `./opkg/opkg repo index opkg/repo-site`
- commit + push
- verify Pages URL serves:
  - `/index.json`
  - `/packages/<file>.opk`
