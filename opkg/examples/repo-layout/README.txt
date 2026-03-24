Static HTTP repo example:

repo/
  index.json
  packages/
    hello-sample-1.0.0-x86_64.opk
    hello-extra-1.1.0-x86_64.opk

In production, host this directory tree via HTTP/HTTPS.

Note:
- `index.json` in this example uses placeholder checksums.
- Replace each checksum with the real `sha256:<hex>` for package files.
