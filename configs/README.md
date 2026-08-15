## Configs

It's a good idea to save build configurations grouped by OS and then by board (see the pattern below).

```text
configs/<os>/<board>/<name>/
```

For NuttX, a saved configuration is the `defconfig` produced by `make savedefconfig` inside `external/nuttx`, for example
`configs/nuttx/raspberrypi-pico-2/nsh-blinky/defconfig`.
Restore it by copying it over the board's config before running `./tools/configure.sh`.

A second OS gets its own directory here (`configs/zephyr/`, and so on) with whatever file that OS uses to pin a build.
