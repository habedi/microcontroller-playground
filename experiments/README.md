## Experiments

One directory per experiment.
Each directory has a `README.md` that says:

- Which board it runs on.
- Which OS or SDK it uses (NuttX, bare metal, and so on).
- How to build and flash it, as exact commands.
- What it showed, once it has run.

Name a directory after what the experiment does, not after the board: `blinky`, `psram-speed`, `wifi-scan`.
The `README.md` carries the board and the OS, so the same idea can be redone later on other hardware.
