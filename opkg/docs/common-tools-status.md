# Common Tools Packaging Status

This tracks practical package availability in the current Obelisk ecosystem.

## Available in repository examples

- `hello-sample` (demo package)
- `grep` (wrapper package)
- `sed` (wrapper package)

These are generated into `opkg/repo-site/packages/` by:

```bash
./opkg/tools/build-common-web-repo.sh
```

## Pending native ports

- `nano`
- `gcc`

These require additional userspace/runtime compatibility and porting work
before they are safe to publish as normal `opkg` packages for Obelisk runtime.

## Policy

- Do not publish packages as "stable" unless they run correctly inside booted
  Obelisk.
- Wrapper packages must clearly describe what binary they dispatch to.
- Prefer reproducible package builds from source/layout in-tree.
