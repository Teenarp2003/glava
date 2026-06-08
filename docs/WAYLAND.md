# Building GLava with the Wayland (Hyprland) backend

This document explains how to compile GLava from scratch with the native
**Wayland** windowing backend (EGL + `wlr-layer-shell`) and run it as a
click-through desktop-background visualizer on **Hyprland** (or any
wlroots-based compositor: Sway, river, Wayfire, etc.).

It also documents how to **completely remove the Wayland backend** and return
the tree to the upstream X11-only state if it does not work for you.

> The Wayland backend is additive: it implements the existing `struct gl_wcb`
> windowing interface (`glava/render.h`) alongside the X11/GLX backend. The
> OpenGL renderer and all GLSL shaders are unchanged.

---

## 1. Prerequisites

You need a Linux machine running a wlroots-based compositor (this guide
targets Hyprland). Building on macOS will not work — Wayland is Linux-only.

### Required packages

> The `wlr-layer-shell-unstable-v1.xml` protocol file is **vendored** in this
> repository (`glava/protocols/`), so you do **not** need `wlr-protocols`
> installed. Only `wayland-protocols` (for `xdg-shell.xml`) and
> `wayland-scanner` are required at build time.

**Arch Linux / Hyprland (recommended):**

```bash
sudo pacman -S --needed \
    base-devel meson ninja git \
    wayland wayland-protocols \
    mesa libglvnd \
    libpulse
```

**Debian / Ubuntu:**

```bash
sudo apt-get install -y \
    build-essential meson ninja-build git pkg-config \
    libwayland-dev wayland-protocols \
    libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dev \
    libpulse-dev
```

(`libwayland-dev` provides `wayland-scanner`, `libwayland-client` and
`libwayland-egl`.)

### What each dependency is for

- `wayland` / `libwayland-dev` — the Wayland client library (`libwayland-client`,
  `libwayland-egl`).
- `wayland-protocols` — XML for `xdg-shell`; `wayland-scanner` generates client
  glue at build time.
- `wlr-layer-shell-unstable-v1` — the protocol used to place GLava on the
  desktop **background** layer with click-through input. Its XML is vendored in
  `glava/protocols/`, so no extra package is needed. Override the path with
  `-Dwlr_layer_shell_xml=` if you prefer a system copy.
- `mesa` / `libegl1-mesa-dev` — EGL + desktop OpenGL. The backend requests a
  **desktop GL** context (`eglBindAPI(EGL_OPENGL_API)`) so the existing
  `#version 330 core` shaders run unmodified.
- `libpulse` — PulseAudio/PipeWire audio input (unchanged from upstream).

---

## 2. Compile from scratch

```bash
git clone <your-fork-or-this-repo> glava
cd glava

# Configure: enable the Wayland backend.
# Keep the X11/GLX backend too (recommended) so the binary works on both:
meson setup build -Denable_wayland=true -Ddisable_obs=true

# ...or build a pure-Wayland binary with no X11 dependency at all:
# meson setup build -Denable_wayland=true -Ddisable_glx=true -Ddisable_obs=true

ninja -C build
```

> `-Ddisable_obs=true` is recommended because the (default-on) OBS plugin links
> against Xlib and the OBS headers; omit it only if you actually want the OBS
> plugin and have its dependencies installed. For a pure-Wayland build
> (`-Ddisable_glx=true`) you should keep OBS disabled, otherwise Xlib is pulled
> back in.

To run directly from the build directory without installing, add
`-Dstandalone=true` to the `meson setup` line. Otherwise install system-wide:

```bash
sudo ninja -C build install
sudo ldconfig
```

### Build flags reference

- `-Denable_wayland=true` — compile and link the Wayland backend (default: off).
- `-Ddisable_glx=true` — drop the X11/GLX backend (and X11/Xrender deps).
  Combine with `-Denable_wayland=true` for a pure-Wayland build.
- `-Dwlr_layer_shell_xml=<path>` — override the vendored
  `wlr-layer-shell-unstable-v1.xml` with a system copy (optional).
- `-Ddisable_obs=true` — skip the OBS plugin (recommended; it requires Xlib + OBS).
- `-Dbuildtype=debug` — debug build.
- `-Dstandalone=true` — run from `build/` without installing.

> If you copied the layer-shell XML into the repo, the build looks for it under
> `glava/protocols/` by default; otherwise pass `-Dwlr_layer_shell_xml=...`.

---

## 3. First run on Hyprland

The backend is auto-selected when `WAYLAND_DISPLAY` is set. You can also force it:

```bash
# Copy the default config the first time:
glava --copy-config

# Run (auto-detects Wayland), or force the backend explicitly:
glava
glava --backend wayland --verbose
```

For a desktop-background, click-through visualizer, make sure your config
(`~/.config/glava/rc.glsl`) requests the desktop window type, e.g.:

```glsl
#request setxwintype "desktop"
#request setclickthrough
```

On Wayland this maps to the `wlr-layer-shell` **background** layer with an empty
input region (click-through). Transparency is native — no wallpaper-mirroring
hack is required.

### Optional: launch with Hyprland

Add to `~/.config/hypr/hyprland.conf`:

```conf
exec-once = glava --backend wayland
```

### Verifying it works

- GLava appears **behind** all normal windows.
- Clicks pass **through** to the desktop/windows underneath.
- The visualizer reacts to audio.
- Transparent areas show the wallpaper (compositor alpha blending).

---

## 4. Troubleshooting

- **`wayland-scanner: command not found`** — install `wayland` (Arch) or
  `wayland-protocols` + the `wayland-scanner` binary (it ships with
  `libwayland-bin` on some distros).
- **`wlr-layer-shell-unstable-v1.xml not found`** — install `wlr-protocols` or
  pass `-Dwlr_layer_shell_xml=/path/to/wlr-layer-shell-unstable-v1.xml`.
- **`eglBindAPI`/`No backend available`** — ensure Mesa EGL is installed and
  `WAYLAND_DISPLAY` is set inside your Hyprland session.
- **Black/opaque background** — your config likely isn't transparent; confirm
  `setxwintype "desktop"` and that the EGL alpha config was selected
  (run with `--verbose`).
- **Nothing reacts to audio** — unrelated to the backend; check the PulseAudio /
  PipeWire source with `--audio` / `setsource`.

---

## 5. Uninstalling GLava from your Hyprland environment

If GLava does not function as expected, here is how to fully remove the
installed package and its files from your system. Pick the method that matches
how you installed it.

### First: stop any running instance

```bash
pkill glava
```

Also remove the autostart line you may have added to
`~/.config/hypr/hyprland.conf`:

```conf
# delete this line if present:
exec-once = glava --backend wayland
```

### Method A — you installed with `ninja install` (this guide)

Meson records every installed file, so it can uninstall cleanly **as long as you
still have the same `build/` directory** you installed from:

```bash
cd /path/to/glava
sudo ninja -C build uninstall
sudo ldconfig
```

`ninja uninstall` reads `build/meson-logs/install_log.txt` and deletes exactly
the files it installed (binary, shared library, shaders, resources, headers,
and the OBS plugin / lua modules if those were enabled).

> Note: `ninja uninstall` removes files but not the (now-empty) directories it
> created. Clean those up manually if you like (see Method C).

### Method B — you installed from a distro package

If you installed via your package manager instead of building, remove it the
same way:

```bash
# Arch Linux (glava or glava-git AUR package):
sudo pacman -Rns glava        # or: glava-git

# Debian / Ubuntu:
sudo apt-get remove --purge glava
```

### Method C — manual removal (build dir is gone)

If you no longer have the `build/` directory, delete the installed files by
hand. With the **default** install prefix (`/usr`) and options, these are:

```bash
sudo rm -f  /usr/bin/glava
sudo rm -f  /usr/bin/glava-config                 # only if config tool was built
sudo rm -f  /usr/lib/libglava.so*                 # path may be /usr/lib64 or /usr/lib/x86_64-linux-gnu
sudo rm -f  /usr/include/glava.h
sudo rm -rf /etc/xdg/glava                         # installed shader/module system
sudo rm -rf /usr/share/glava                       # generic resources
sudo rm -rf /usr/share/lua/*/glava-config          # lua modules, if config tool was built
sudo rm -f  /usr/lib/obs-plugins/libglava-obs.so   # only if OBS plugin was built
sudo ldconfig
```

> If you configured a custom `--prefix` or any `*_install_dir` option, adjust
> these paths accordingly. `ninja -C build uninstall` (Method A) avoids this
> guesswork entirely, so prefer it when possible.

### Finally: remove your user config (optional)

`glava --copy-config` creates per-user files. Remove them to fully clean up:

```bash
rm -rf ~/.config/glava
```

After this, GLava is completely removed from your Hyprland environment.
