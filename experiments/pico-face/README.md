## Face on the Pico 2 Display

An expressive face on the 1.3 inch 240x240 panel, driven by whatever Claude
Code is doing. The point is a status display you read at a glance instead of
switching windows: the eyes narrow while a tool runs, the face looks down while
files change, and it looks up and pulses when it is waiting on you.

Board: Raspberry Pi Pico 2 WH with the Waveshare 1.3-inch LCD hat.
OS: Apache NuttX, configuration `configs/nuttx/raspberrypi-pico-2/usbnsh-lcd-wifi`.

### Status

Working. The host tests pass, the image builds, and the face runs on the panel
with all six expressions reachable in any of three presets, which the panel's
own joystick moves between.

Measured on the board:

- 60 frames per second at 240x240 from `face -b`, which is 16.7 ms per frame
  against 14.4 ms of unavoidable SPI time at the 64 MHz clock. So the drawing
  itself costs about 2 ms and the bus is the limit.
- 15.7 percent of one core for the render loop at 30 frames per second, with
  the whole panel redrawn every frame.
- 4 KB of stack, unchanged from the default.
- 7 KB of flash for the original vector face, and 10.5 KB more for the two
  pixel presets, the overlay font, and the controls.
- Around 16 percent of one core in every preset, so the sprite blit and the
  vector drawing cost about the same. The bus is the limit, not the drawing.

### How It Is Put Together

The parts that need a board are kept apart from the parts that do not, so
most of it is testable on the host:

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
- `src/face_preset.c` holds the table of looks. A preset is a name and a
  render function, and `face_render.c` is simply the first row in it.
  `face_pixel.c` and `face_hero.c` are the other two, with the hero's art in
  `face_hero_art.c` and the shared blit and palettes in `face_sprite.c`.
- `src/face_input.c` turns a button mask into an action. It is a pure function
  of the current and previous masks, with no board code, so the edge detection
  is tested on the host.
- `src/face_overlay.c` and `src/face_font.c` draw the debug overlay.
- `src/face_main.c` is the only file that touches the board. It opens
  `/dev/fb0`, runs the render loop at about 30 frames per second, reads the
  current state out of a file, and reads `/dev/buttons`.

Nothing here stores a full frame. One 240x240 frame is 115,200 bytes, so a
bitmap for each expression in each preset would be most of the flash. The
vector and pixel presets draw from shapes instead, which also lets them blend
between expressions. The hero preset does use bitmaps, but they are 32 by 32
and indexed to a 16 entry palette, then scaled up on the way to the panel, so
a pose costs about a kilobyte rather than a hundred.

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

That writes `tools/build/faces.png`, a contact sheet with one row per preset
and one column per expression. Pass `WHEN=<milliseconds>` to sample the
animation at a different moment, and `PALETTE=<0 to 2>` to see the other colour
sets. This is where the art is worth judging, since a round here costs nothing
and a round on the board costs a BOOTSEL press.

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

`quit` is the only way to stop the loop, since `kill` does nothing in this
configuration. The word stays in the file so that every running loop sees it,
and a loop that starts later removes it before its first poll rather than
stopping on it. Hold ignores the expression in the file, not the quit word.

### Presets

A preset is a name and a render function in `src/face_preset.c`, so adding a
look is one new file and one new row. All three share the expression state
machine in `face.c`, which means the hook drives whichever one is showing
without knowing anything about it.

| Preset | What it draws |
| --- | --- |
| `vector` | The original amber face, drawn from shapes. |
| `pixel` | A pixel portrait bust on a 48 by 48 grid, five panel pixels per art pixel. |
| `hero` | A 32 by 32 character sprite, one pose per expression. |

The first two take their whole shape from `struct face_pose`, so blinks and
pupil drift work in both. The hero picks its pose from the state and the clock
instead, because at 32 pixels an eyelid is one pixel and a blink would not
read. Its idle motion is a one pixel bob of the whole sprite rather than a
second frame, which animates it for no extra art.

Sprite art lives in `src/face_hero_art.c` as rows of characters, one per pixel.
A dot is transparent and a hex digit picks a palette entry, so the art is
editable in a text editor and readable in a diff. `test_sprite.c` checks that
every row is the declared width and holds only characters the palette can
resolve, which is the failure this format invites: a short row shifts every
pixel after it without breaking anything else.

Palettes are held apart from the art, so all three colour sets work on every
sprite and cycling them is nearly free.

### Panel Controls

The Waveshare Pico-LCD-1.3 has a five way joystick and four keys. The pinout
and the patch that declares them are in `docs/raspberrypi-pico-2.md`.

| Control | Action |
| --- | --- |
| Joystick left, right | Previous, next preset |
| Joystick up, down | Brighter, dimmer |
| Joystick press | Nothing yet |
| A | Hold, which ignores the state file |
| B | Next palette |
| X | Next animation speed |
| Y | Debug overlay |

Hold is worth knowing about. The hook rewrites `/tmp/face` on every tool call,
so without it an expression chosen at the board is replaced within seconds.

The face runs without any of this. If `/dev/buttons` is missing, because the
build has no button support or the driver did not register, it says so once
and carries on reading the state file.

The mapping itself is a pure function of the current and previous button
masks, in `face_input.c`, so the edge detection that stops a held button
repeating is covered by `test_input.c` rather than by pressing buttons.

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
- The vector preset still changes brow position only, not brow shape. The pixel
  portrait angles its brows and carries the expression better for it, so the
  same treatment is the obvious next change to the vector look.
- Brightness dims the rendered pixels rather than the backlight. The backlight
  pad is a plain GPIO in the board glue and only knows on and off, so real
  dimming means moving that pin to `RP23XX_PWM`.
- The joystick press is read but unbound.
