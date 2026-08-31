## Face on the Pico 2 Display

An expressive face on the 1.3 inch 240x240 panel, driven by whatever Claude
Code is doing. The point is a status display you read at a glance instead of
switching windows: the eyes narrow while a tool runs, the face looks down while
files change, and it looks up and pulses when it is waiting on you.

Board: Raspberry Pi Pico 2 WH with the Waveshare 1.3 inch LCD hat.
OS: Apache NuttX, configuration `configs/nuttx/raspberrypi-pico-2/usbnsh-lcd-wifi`.

### Status

Working. The host tests pass, the image builds, and the face runs on the panel
with all six expressions reachable.

Measured on the board:

- 60 frames per second at 240x240 from `face -b`, which is 16.7 ms per frame
  against 14.4 ms of unavoidable SPI time at the 64 MHz clock. So the drawing
  itself costs about 2 ms and the bus is the limit.
- 15.7 percent of one core for the render loop at 30 frames per second, with
  the whole panel redrawn every frame.
- 4 KB of stack, unchanged from the default.
- 7 KB of flash added to the image.

### How It Is Put Together

Three pieces, each testable on its own:

- `src/face.c` is the expression state machine. Six states in, eight numbers
  out, blended over 220 ms so a change never snaps. No board code and no
  floating point, and every pose is a pure function of the state, the time the
  state was entered, and the current time. That last property is what lets the
  render loop drop frames without the animation drifting, and what lets a test
  reproduce a frame exactly.
- `src/face_render.c` draws a pose into an RGB565 surface the caller owns.
  Shapes are rounded rectangles and a tapered parabola, so an expression costs
  a handful of numbers rather than a 115 KB bitmap, and blending two
  expressions is interpolation. It knows nothing about NuttX.
- `src/face_main.c` is the only file that touches the board. It opens
  `/dev/fb0`, runs the render loop at about 30 frames per second, and reads the
  current state out of a file.

Drawing procedurally rather than from bitmaps is what makes the whole thing
fit. One 240x240 frame is 115,200 bytes, so a bitmap for each of six
expressions would be most of the flash and could not blend between them.

### Host Tests

No board and no cross compiler needed:

```shell
make -C experiments/pico-face/test test
```

The address and undefined behaviour sanitizers are on by default, since an out
of bounds write in the drawing code is the failure worth catching. Three tests
earned their place by catching real bugs:

- The widest row of an eye has to be its middle. The first version of the
  corner arithmetic measured the inset from the wrong side and drew an
  hourglass, and every other test still passed.
- A smile has to dip in the middle, because y grows downwards. Getting the sign
  wrong drew a frown for `done` and a smile for `failed`.
- Moving the pupil must not light any pixel outside the eye. It used to spill
  over a narrowed lid and read as a dark blob hanging off the face.

### Looking at It Without Flashing

```shell
make -C experiments/pico-face/tools preview
```

That writes `tools/build/faces.png`, a contact sheet of all six expressions.
Pass `WHEN=<milliseconds>` to sample the animation at a different moment.

### Build and Flash

The application lives here rather than in `external/nuttx-apps`, and NuttX
reaches it through the `apps/external` symlink that nuttx-apps documents and
covers in its own `.gitignore`:

```shell
make nuttx-distclean
make nuttx-link-app EXTERNAL_APP=experiments/pico-face
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/raspberrypi-pico-2/usbnsh-lcd-wifi
make nuttx-build
make flash-pico-uf2
```

The flash needs the BOOTSEL button held while the board is plugged in.

One consequence of building in place: the object files and dependency lists
land under `experiments/` instead of under `external/`, and their names embed
an absolute path. `.gitignore` covers them.

### Running It

```
nsh> face &          # render loop in the background
nsh> face working    # change the expression
nsh> face done
nsh> face quit       # stop the loop
nsh> face -b         # frames per second on this panel
```

`face` with an argument writes the word into `/tmp/face`, which is what the
render loop polls, so anything that can write that file can drive the face.
`/tmp` is mounted at boot by the `rcS` script described in
`docs/raspberrypi-pico-2.md`.

The six words are `idle`, `working`, `editing`, `waiting`, `failed`, and
`done`. An unknown word is rejected where it is typed rather than ignored by
the render loop.

### Driving It from Claude Code

`tools/face-hook.py` is the bridge. Claude Code runs it on each event, it works
out which expression the event means, and the board follows a second or two
later.

| Hook | Word |
| ---- | ---- |
| `SessionStart`, `SessionEnd` | `idle` |
| `PreToolUse` | `working`, or `editing` for Edit, Write, and NotebookEdit |
| `PostToolUse` when the response looks like an error | `failed` |
| `Notification` | `waiting` |
| `Stop`, `SubagentStop` | `done` |

A hook runs in front of the thing it reports on, so it has to be quick, and a
serial round trip to the board takes a second or two. So the script splits in
two. The half a hook calls decides the word, writes it to a spool file, starts a
detached copy of itself, and exits. Measured at 18 to 22 ms, and it imports no
serial code at all. The detached half takes a lock and does the talking. If
another copy already holds the lock it gives up rather than queueing, because
the copy holding it will read whatever the newest word is. The last event wins
and a backlog cannot build up.

It never prints anything and always exits zero. A hook that complains gets in
the way of the session it is meant to describe.

The wiring lives in `.claude/settings.local.json`, which this repository's
`.gitignore` covers, so it stays on one machine. The script in `tools/` is
committed; only the six hook entries pointing at it are local.

Two limits worth knowing:

- Failure detection is a guess. There is no dedicated field saying a tool
  failed, so the script reads the response the way a person would and will miss
  cases.
- The spool and lock paths deliberately avoid `tempfile.gettempdir()`, which
  follows `TMPDIR`. The two halves have to agree on the path, and `TMPDIR` is
  not the same in every context a hook runs from. The first version used it and
  the two halves wrote and read different files, so the timings looked right
  while nothing reached the board.

### Not Done Yet

- The renderer redraws the whole panel every frame, which is where the 15.7
  percent goes. The dirty box is already in the interface, and a blink touches
  two small rectangles, so partial redraw should cut this by most of itself.
- No brow shape change between expressions, only vertical position. Angling the
  brows would carry more of the expression than anything else left on this list.
- The four buttons and the joystick on the hat are unused. They are plain GPIO
  and want a `djoystick` driver.
