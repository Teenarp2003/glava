# GLava on Hyprland - pre-flight checklist

A quick checklist to get the native Wayland backend running on Hyprland.
See [WAYLAND.md](WAYLAND.md) for full build/uninstall details.

## Dependencies (Arch / Hyprland)

- [ ] Build tools: `sudo pacman -S --needed base-devel meson ninja git pkgconf`
- [ ] Wayland + GL + audio: `sudo pacman -S --needed wayland wayland-protocols mesa libglvnd libpulse`

> The `wlr-layer-shell` protocol XML is vendored in the repo, so `wlr-protocols`
> is not required.

## Build

- [ ] Configure (hybrid X11 + Wayland, OBS off):
      `meson setup build -Denable_wayland=true -Ddisable_obs=true`
      - For a pure-Wayland binary add `-Ddisable_glx=true`
- [ ] Compile: `ninja -C build`
- [ ] Install: `sudo ninja -C build install && sudo ldconfig`
      - Or use `-Dstandalone=true` to run from `build/` without installing

## Configure

- [ ] Copy the default config: `glava --copy-config`
- [ ] Edit `~/.config/glava/rc.glsl`:
  - [ ] `#request setopacity "native"` (real transparency; NOT `"xroot"`)
  - [ ] `#request setxwintype "desktop"` (optional; background is the default)

## Verify the session

- [ ] `echo $WAYLAND_DISPLAY` prints something (e.g. `wayland-1`)
- [ ] `echo $XDG_CURRENT_DESKTOP` prints `Hyprland`

## Verify audio (PulseAudio API via PipeWire)

- [ ] `systemctl --user status pipewire-pulse` is active
- [ ] `pactl info` succeeds
- [ ] (default `setsource "auto"` uses the default sink's monitor)

## First run

- [ ] Run from a terminal first: `glava --backend wayland --verbose`
- [ ] Confirm: appears behind windows, clicks pass through, reacts to audio,
      wallpaper shows through transparent areas
- [ ] Only then autostart it: add to `~/.config/hypr/hyprland.conf`:
      `exec-once = glava --backend wayland`

## Known v1 limitations

- [ ] Binds the first output only (no multi-monitor selection yet)
- [ ] `setopacity "xroot"` (wallpaper mirroring) is unsupported - native
      compositor transparency is used instead

## If something breaks

Have these ready when reporting:

- [ ] Full output of `glava --backend wayland --verbose`
- [ ] Output of `pactl info`
- [ ] Whether you see the visualizer at all (vs. black / transparent)
