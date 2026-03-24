Drop optional runtime files in this directory to include them in build/rootfs.tar.

Typical layout:
- rootfs-overlay/bin/busybox
- rootfs-overlay/bin/zsh
- rootfs-overlay/usr/bin/ls
- rootfs-overlay/etc/zshrc

Anything placed here is copied into the rootfs image during `make rootfs` and `make iso`.

Notes:
- Prefer static binaries while syscall coverage is still under development.
- If `/bin/busybox` exists and `/bin/sh` does not, the build creates `/bin/sh -> /bin/busybox`.
