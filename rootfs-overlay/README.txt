Drop optional runtime files in this directory to include them in build/rootfs.tar.

Typical layout:
- rootfs-overlay/bin/rockbox
- rootfs-overlay/bin/osh
- rootfs-overlay/usr/bin/ls
- rootfs-overlay/etc/profile

Anything placed here is copied into the rootfs image during `make rootfs` and `make iso`.

Notes:
- Prefer static binaries while syscall coverage is still under development.
- If `/bin/rockbox` exists and `/bin/osh` does not, the build creates `/bin/osh -> /bin/rockbox`.
