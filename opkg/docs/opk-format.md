# `.opk` Format v1

`.opk` is Obelisk's native package container format.

## Design Goals

- inspectable with standard UNIX tooling
- minimal metadata requirements
- stable and boring over complex
- easy to parse in D

## Physical Layout (v1)

An `.opk` file is a tar archive containing exactly:

- `meta.json`
- `files.tar`

Example:

```text
hello-1.0-x86_64.opk
  meta.json
  files.tar
```

## Metadata (`meta.json`)

Required fields:

- `name`
- `version`
- `arch`
- `summary`
- `description`
- `depends` (array)
- `maintainer`
- `section`

Optional fields:

- `provides` (array)
- `conflicts` (array)

## Payload (`files.tar`)

`files.tar` is the package payload rooted at `/` semantics (relative file paths
inside the tar, installed onto the target root).

v1 behavior notes:

- file paths from `files.tar` are normalized into manifest paths like
  `/usr/bin/foo`
- directory-only tar entries are ignored in installed-file manifests
- `opkg remove` deletes only files recorded in the manifest

## Build input layout

`opkg build` accepts:

- `meta.json` and `files.tar` directly, or
- `meta.json` and `rootfs/` (in which case `files.tar` is generated from
  `rootfs/` during the build)

## Checksums and verification (v1)

- repository index stores package checksum (sha256 planned)
- package-level signature is **not mandatory in v1**
- signing hooks are reserved for later versions

## Reserved future extensions

- install/remove script hooks
- package signatures
- compressed payload variants
