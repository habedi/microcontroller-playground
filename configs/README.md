## Configs

Saved build configurations are grouped by OS and board:

```text
configs/<os>/<board>/<name>/
```

For NuttX, save the current configuration with:

```shell
make nuttx-save-config SAVED_CONFIG=configs/nuttx/raspberrypi-pico-2/usbnsh-tools
```

Restore it with:

```shell
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/raspberrypi-pico-2/usbnsh-tools
```

A second OS gets its own directory here (`configs/zephyr/`, and so on) with whatever file that OS uses to pin a build.
