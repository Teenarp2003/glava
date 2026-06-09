**GLava** is a general-purpose, highly configurable OpenGL audio spectrum visualizer, originally developed by [jarcode-foss](https://github.com/jarcode-foss/glava).

This is a fork of the original repo which adds native support for Wayland, with other features like live-color reload.

**Wayland / Hyprland:** an experimental native Wayland backend (EGL + `wlr-layer-shell`) renders GLava as a click-through desktop-background layer on wlroots-based compositors such as Hyprland. Build with `-Denable_wayland=true`. 

## 1. Prerequisites
You need a Linux machine running a wlroots-based compositor (this guide
targets Hyprland).

### Required packages

> The `wlr-layer-shell-unstable-v1.xml` protocol file is **vendored** in this
> repository (`glava/protocols/`), so you do **not** need `wlr-protocols`
> installed. Only `wayland-protocols` (for `xdg-shell.xml`) and
> `wayland-scanner` are required at build time.

**Arch Linux :** 

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

**Compiling:**

```bash
$ git clone https://github.com/Teenarp2003/glava
$ cd glava
$ meson setup build --prefix /usr -Denable_wayland=true -Ddisable_obs=true # Build with both X11 and Wayland support
$ meson setup build --prefix /usr -Denable_wayland=true -Ddisable_glx=true -Ddisable_obs=true # Build with only the Wayland backend
$ sudo ninja -C build
$ sudo ninja -C build install
$ sudo ldconfig
```

> `-Ddisable_obs=true` is recommended because the (default-on) OBS plugin links
> against Xlib and the OBS headers; omit it only if you actually want the OBS
> plugin and have its dependencies installed. For a pure-Wayland build
> (`-Ddisable_glx=true`) you should keep OBS disabled, otherwise Xlib is pulled
> back in.


You can pass `-Dbuildtype=debug` to Meson for debug builds of glava, and `-Dstandalone=true` to run glava directly from the `build` directory.

Note that versions since `2.0` use Meson for the build system, although the `Makefile` will remain to work identically to earlier `1.xx` releases (with new features disabled). Package maintainers are encouraged to use Meson directly instead of the Make wrapper.

Don't forget to run `sudo ldconfig` after installing.

Currently this project is not yet available for direct installation through any package manager. Support for installation would be available soon.

## Usage 

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
### Live color updates

GLava's pipe bindings let you drive colors live without restarting. The `bars`
module color is `#define COLOR @fg:mix(...)`; the `@name:default` syntax uses the
live uniform `_IN_name` when GLava is launched with `--pipe="name:vec4"`, else the
inline default.

For a gradient fed from your pywal palette, color `bars.glsl` with two pipes:

```glsl
#define COLOR mix(@low:#3366b2 , @high:#a0a0b2 , clamp(d / GRADIENT, 0, 1))
```

The spaces before the commas are required: GLava's binding parser consumes the
single character that ends a matched `@name` binding, so the argument separator
must be whitespace (the comma then survives as the literal separator).

launch with `glava --backend wayland --pipe="low:vec4" --pipe="high:vec4"`
(do **not** add `--desktop` — see the troubleshooting note below), and push lines
like `low=#1d2021` / `high=#a89984` to its stdin whenever the theme changes. Ready-to-install scripts and a drop-in `bars.glsl` are in
[contrib/pywal/](../contrib/pywal/) (see its `README.md`).

```bash
#Copy over the required files from repo to configuration directory:
$ cp <GLava repo>/contrib/pywal/bars.glsl ~/.config/glava/bars.glsl
# Run the start scripts from the glava directory. 
$ ./contrib/pywal/glava-pywal-start
# Use the glava-pywal-update script to update colors without restart. 
$ ./contrib/pywal/glava-pywal-update
```


## [Configuration](https://github.com/jarcode-foss/glava/wiki)

GLava will start by looking for an entry point in the user configuration folder (`~/.config/glava/rc.glsl`), and will fall back to loading from the shader installation folder (`/etc/xdg/glava`). The entry point will specify a module to load and should set global configuration variables. Configuration for specific modules can be done in their respective `.glsl` files, which the module itself will include.

You should start by running `glava --copy-config`. This will copy over default configuration files and create symlinks to modules in your user config folder. GLava will either load system configuration files or the user provided ones, so it's not advised to copy these files selectively.

To embed GLava in your desktop (for EWMH/X11  window managers), run it with the `--desktop` flag and then position it accordingly with `#request setgeometry x y width height` in your `rc.glsl`.

For more information, see the [main configuration page](https://github.com/jarcode-foss/glava/wiki).

## 5. Uninstalling GLava 

If GLava does not function as expected, here is how to fully remove the
installed package and its files from your system. 

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

### Method A — installation was done with `ninja install`  

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

### Method B — manual removal 

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

## Desktop window compatibility

GLava aims to be compatible with _most_ EWMH compliant window managers. Below is a list of common window managers and issues specific to them for trying to get GLava to behave as a desktop window or widget:

| WM | ! | Details
| :---: | --- | --- |
| Mutter (GNOME, Budgie) | ![-](https://placehold.it/15/118932/000000?text=+) | `"native"` (default) opacity should be used
| KWin (KDE) | ![-](https://placehold.it/15/118932/000000?text=+) | "Show Desktop" [temporarily hides GLava](https://github.com/jarcode-foss/glava/issues/4#issuecomment-419729184)
| Openbox (LXDE or standalone) | ![-](https://placehold.it/15/118932/000000?text=+) | No issues
| Xfwm (XFCE) | ![-](https://placehold.it/15/118932/000000?text=+) | No issues
| Fluxbox | ![-](https://placehold.it/15/118932/000000?text=+) | No issues
| IceWM | ![-](https://placehold.it/15/118932/000000?text=+) | No issues
| Bspwm | ![-](https://placehold.it/15/118932/000000?text=+) | No issues
| SpectrWM |
| Herbstluftwm | ![-](https://placehold.it/15/118932/000000?text=+) | `hc rule windowtype~'_NET_WM_WINDOW_TYPE_DESKTOP' manage=off` can be used to unmanage desktop windows
| Unity | ![-](https://placehold.it/15/118932/000000?text=+) | No issues
| AwesomeWM | ![-](https://placehold.it/15/118932/000000?text=+) | Defaults to unmanaged
| i3 (and i3-gaps) | ![-](https://placehold.it/15/118932/000000?text=+) | Defaults to unmanaged
| spectrwm | ![-](https://placehold.it/15/118932/000000?text=+) | Defaults to unmanaged
| EXWM | ![-](https://placehold.it/15/f03c15/000000?text=+) | EXWM does not have a desktop, and forces window decorations
| Enlightenment | ![-](https://placehold.it/15/1589F0/000000?text=+) | Needs testing
| Xmonad | ![-](https://placehold.it/15/118932/000000?text=+) | No issues after enabling ewmh hints via `XMonad.Hooks.EwmhDesktops.ewmh`
| Any non EWMH-compliant WM | ![-](https://placehold.it/15/f03c15/000000?text=+) | Window types and hints will not work if the window manager does not support the EWMH standards.

Note that some WMs listed without issues have specific overrides when using the `--desktop` flag. See `shaders/env_*.glsl` files for details.

## Reading from MPD's FIFO output

Add the following to your `~/.config/mpd.conf`:

```
audio_output {
    type                    "fifo"
    name                    "glava_fifo"
    path                    "/tmp/mpd.fifo"
    format                  "22050:16:2"
}
```

Note the `22050` sample rate -- this is the reccommended setting for GLava. Restart MPD (if nessecary) and start GLava with `glava --audio=fifo`.

## Using GLava with OBS

GLava installs a plugin for rendering directly to an OBS scene, if support was enabled at compile-time. This is enabled by default in Meson, but it is overridden to disabled in the `Makefile` for build compatibility.

To use the plugin, simply select `GLava Direct Source` from the source list in OBS and position the output accordingly. You can provide options to GLava in the source properties.

Note that this only works for the default GLX builds of both OBS and GLava. This feature will not work if OBS was compiled with EGL for context creation, or if GLava is using GLFW.

## Performance

GLava will have a notable performance impact by default due to reletively high update rates, interpolation, and smoothing. Because FFT computations are (at the moment) performed on the CPU, you may wish to _lower_ `setsamplesize` and `setbufsize` on old hardware.

However, there is functionality to prevent GLava from unessecarily eating resources. GLava will always halt completely when obscured, so a fullscreen application covering the visualizer should enounter no issues (ie. games). If you wish for GLava to halt rendering when _any_ fullscreen application is in focus regardless of visibility, you can set `setfullscreencheck` to `true` in `rc.glsl`.

Any serious performance and/or updating issues (low FPS/UPS) should be reported. At a minimum, modules should be expected to run smoothly on Intel HD graphics and software rasterizers like `llvmpipe`.

## Licensing

GLava is licensed under the terms of the GPLv3, with the exemption of `khrplatform.h`, which is licensed under the terms in its header. GLava includes some (heavily modified) source code that originated from [cava](https://github.com/karlstav/cava), which was initially provided under the MIT license. The source files that originated from cava are the following:

- `[cava]/input/fifo.c -> [glava]/fifo.c`
- `[cava]/input/fifo.h -> [glava]/fifo.h`
- `[cava]/input/pulse.c -> [glava]/pulse_input.c`
- `[cava]/input/pulse.h -> [glava]/pulse_input.h`

The below copyright notice applies for the original versions of these files:

`Copyright (c) 2015 Karl Stavestrand <karl@stavestrand.no>`

GLava also contains GLFFT, an excellent FFT implementation using Opengl 4.3 compute shaders. This was also initiallly provided under the MIT license, and applies to the following source files (where `*` refers to both `hpp` and `cpp`):

- `glfft/glfft.*`
- `glfft/glfft_common.hpp`
- `glfft/glfft_gl_interface.*`
- `glfft/glfft_interface.hpp`
- `glfft/glfft_wisdom.*`

The below copyright notice applies for the original versions of these files:

`Copyright (c) 2015 Hans-Kristian Arntzen <maister@archlinux.us>`

**The noted files above are all sublicensed under the terms of the GPLv3**. The MIT license is included for your convenience and to satisfy the requirements of the original license, although it no longer applies to any code in this repository. You will find the original copyright notice and MIT license in the `LICENSE_ORIGINAL` file for cava, or `glfft/LICENSE_ORIGINAL` for GLFFT.

The below copyright applies for the modifications to the files listed above, and the remaining sources in the repository:
`Copyright (c) 2017 Levi Webb`


