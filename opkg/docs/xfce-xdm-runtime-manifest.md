# XFCE + XDM Runtime Manifest (First Real Login)

This manifest defines the minimum package scaffolding and runtime payload expectations to reach a real graphical login path:

`desktop-session xdm -> xdm -> Xsession -> xfce4-session`

## Scaffold packages

The following scaffold packages now exist under `opkg/examples/`:

- `xorg` (existing)
- `xfce` (existing)
- `desktop-base` (new)
- `gtk3-runtime` (new)
- `xfce-runtime` (new)
- `xdm` (new)

## Profile behavior

`opkg install-profile` now looks for these in priority order:

- `xorg` profile:
  - required: `xorg` | `xorg-base` | `xorg-server`
  - optional: `desktop-base` | `x11-runtime` | `desktop-runtime`
- `xfce` profile:
  - required: `xorg` chain above
  - optional: `desktop-base` chain
  - optional: `gtk3-runtime` | `gtk-runtime` | `gtk-stack`
  - required: `xfce` | `xfce-runtime` | `xfce4` | `xfce-desktop`
- `xdm` profile:
  - required: same `xorg` + `xfce` requirements
  - optional: same `desktop-base` and `gtk3-runtime` chains
  - optional-but-preferred: `xdm` | `x11-xdm` | `xorg-xdm` | `display-manager`

## Minimum runtime payload targets

### `desktop-base`

Drop baseline X11/font libs and assets, typically:

- `/usr/lib/libX11.so*`
- `/usr/lib/libXext.so*`
- `/usr/lib/libXrender.so*`
- `/usr/lib/libXrandr.so*`
- `/usr/lib/libXfixes.so*`
- `/usr/lib/libXi.so*`
- `/usr/lib/libxcb*.so*`
- `/usr/lib/libfontconfig.so*`
- `/usr/lib/libfreetype.so*`
- `/usr/share/fonts/*`

### `gtk3-runtime`

Drop GTK runtime stack, typically:

- `/usr/lib/libgtk-3.so*`
- `/usr/lib/libgdk-3.so*`
- `/usr/lib/libglib-2.0.so*`
- `/usr/lib/libgobject-2.0.so*`
- `/usr/lib/libgio-2.0.so*`
- `/usr/lib/libpango*.so*`
- `/usr/lib/libcairo.so*`
- `/usr/lib/libatk*.so*`

### `xfce-runtime`

Minimum binaries:

- `/usr/bin/xfce4-session` (or `/usr/bin/startxfce4`)
- `/usr/bin/xfwm4`
- `/usr/bin/xfsettingsd`
- `/usr/bin/xfce4-panel`
- `/usr/bin/xfdesktop`

Recommended:

- `/usr/bin/xfce4-terminal`
- `/etc/xdg/xfce4/**`
- `/usr/share/xfce4/**`
- `/usr/share/icons/**`

### `xdm`

Minimum files:

- `/usr/bin/xdm`
- `/etc/X11/xdm/xdm-config`
- `/etc/X11/xdm/Xservers`
- `/etc/X11/xdm/Xsession`
- `/etc/X11/xdm/Xstartup`
- `/etc/X11/xdm/Xsetup`
- `/etc/X11/xdm/Xresources`

## Bring-up checklist

### Host-side payload import helper

Use the helper to import real binaries/libs into scaffold packages:

- from extracted rootfs:
  - `tools/desktop/import-linux-desktop-payloads.sh --source-root /path/to/extracted-root`
- from `.deb` payload directory:
  - `tools/desktop/import-linux-desktop-payloads.sh --deb-dir /path/to/debs`

The helper copies runtime payloads into:

- `opkg/examples/desktop-base/rootfs`
- `opkg/examples/gtk3-runtime/rootfs`
- `opkg/examples/xfce-runtime/rootfs`
- `opkg/examples/xdm/rootfs`

and verifies required binaries (`xfce4-session`, `xdm`) are present.

### Obelisk executable compatibility

Dynamic ELF execution is now enabled by default (`EXPERIMENTAL_DYNAMIC_ELF=1` in top-level/kernel makefiles) to support imported desktop runtime binaries and shared-library interpreters.

1. Build fresh ISO (`make iso`).
2. Boot Obelisk.
3. Install profiles:
   - `opkg install-profile xorg`
   - `opkg install-profile xfce`
   - `opkg install-profile xdm`
4. Run `desktop-session xdm`.
5. If fallback happens, verify missing runtime binaries from this manifest and repack the corresponding scaffold package.

