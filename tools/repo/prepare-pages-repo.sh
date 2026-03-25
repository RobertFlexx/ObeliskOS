#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <packages-dir> <pages-repo-dir>" >&2
  echo "example: $0 build/obelisk-pkgs ../obelisk-packages-pages" >&2
  exit 1
fi

PKG_SRC="$1"
PAGES_DIR="$2"

if [ ! -d "$PKG_SRC" ]; then
  echo "error: packages directory does not exist: $PKG_SRC" >&2
  exit 1
fi

mkdir -p "$PAGES_DIR/packages"
cp -f "$PKG_SRC"/*.opk "$PAGES_DIR/packages/" 2>/dev/null || true

if [ ! -x "./opkg/opkg" ]; then
  echo "building host opkg helper..."
  (cd opkg && dub build)
fi

./opkg/opkg repo index "$PAGES_DIR"

cat > "$PAGES_DIR/index.html" <<'EOF'
<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <title>Obelisk Packages</title>
  </head>
  <body>
    <h1>Obelisk Package Repository</h1>
    <p>This repository is intended for <code>opkg</code> clients.</p>
    <ul>
      <li><a href="./index.json">index.json</a></li>
      <li><a href="./packages/">packages/</a></li>
    </ul>
  </body>
</html>
EOF

cat > "$PAGES_DIR/README.md" <<'EOF'
# Obelisk Packages (GitHub Pages)

This repository is served as a static package repository for `opkg`.

## Publish

1. Commit and push this repository.
2. Enable GitHub Pages (branch root).
3. Use the resulting URL in Obelisk installer repo prompt.

## repos.conf line

desktop https://<your-user>.github.io/<repo-name>

Optional index pin:

desktop https://<your-user>.github.io/<repo-name> sha256:<index-json-hash>
EOF

echo
echo "Repository prepared at: $PAGES_DIR"
echo "Next: commit/push it and enable GitHub Pages."
