# Live pywal colors for GLava (Wayland)

Drive the GLava `bars` gradient from your [pywal](https://github.com/dylanaraps/pywal)
palette, updated instantly on theme change with no GLava restart. It uses
GLava's built-in pipe bindings (`--pipe`) fed over stdin.

These files are version-controlled here because they live outside the repo on a
running system; copy them into place on your Hyprland machine as below.

## Files

- `bars.glsl` - drop-in for `~/.config/glava/bars.glsl`; only the `COLOR` line
  changed to `mix(@low:..., @high:..., ...)` (two live pipe colors).
- `glava-pywal-update` - reads `~/.cache/wal/colors.json` and appends the colors
  to GLava's feed file.
- `glava-pywal-start` - launches GLava reading the feed via `tail -f` with
  `--pipe="low:vec4" --pipe="high:vec4"`.

## Install (on the Hyprland machine)

```bash
# 1. Bar colors (run `glava --copy-config` first if you haven't):
cp contrib/pywal/bars.glsl ~/.config/glava/bars.glsl

# 2. Scripts:
mkdir -p ~/.config/bin
cp contrib/pywal/glava-pywal-update contrib/pywal/glava-pywal-start ~/.config/bin/
chmod +x ~/.config/bin/glava-pywal-update ~/.config/bin/glava-pywal-start

# 3. Autostart in Hyprland (~/.config/hypr/hyprland.conf):
#    remove any old glava / hyprwinwrap autostart, then add:
#       exec-once = ~/.config/bin/glava-pywal-start

# 4. Hook your pywal theme switch so colors propagate live. Wherever you run
#    `wal`, append the updater, e.g.:
#       wal -i "$IMAGE"
#       ~/.config/bin/glava-pywal-update
```

Requires `jq` (palette parsing) and a GLava built with the Wayland backend.

## How it works

`@low:#3366b2` in `bars.glsl` compiles to the live uniform `_IN_low` when GLava
is started with `--pipe="low:vec4"`, otherwise it falls back to the inline
default color. `glava-pywal-update` appends lines like:

```
low=#1d2021
high=#a89984
```

to the feed file; GLava reads them from stdin and updates the gradient on the
next frame. `tail -n +1 -f` keeps stdin open so the pipe never closes.

## Tuning

- Change which palette entries form the gradient by editing the `color1` /
  `color7` indices in `glava-pywal-update`.
- Keep `#request setopacity "native"` in `rc.glsl` so transparency still works.
- Quick test without pywal:
  `printf 'low=#ff0000\nhigh=#00ff00\n' >> "$XDG_RUNTIME_DIR/glava-pywal.feed"`
