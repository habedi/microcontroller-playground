/****************************************************************************
 * experiments/pico-face/src/face_crab.c
 *
 * The crab preset: an angry crustacean whose shell is its face, after the
 * well known drawing.  Drawn from shapes on a 40 by 40 grid blown up to the
 * panel, with a dark orange outline under every orange shape, on the white
 * ground of the original.
 *
 * The face takes its expression from the pose like the other drawn presets,
 * with two twists: the brows carry an anger bias, so the crab is grumpy at
 * rest and furious when a tool fails, and the mouth is never quite shut,
 * so its teeth show at rest and a frown opens it into a shout.
 *
 ****************************************************************************/

#include <stddef.h>

#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The art grid.  240 divides by 40 exactly, giving a 6 pixel block. */

#define GRID 40

/* How much angrier than the pose the crab always is, on the brow scale, and
 * how far open its mouth always is, on the mouth scale.
 */

#define ANGER_BIAS 500
#define MOUTH_BIAS 300

/* Colours.  The crab keeps its own rather than using the shared palettes,
 * because a purple crab is not the crab in the drawing.
 */

#define C_BG      face_rgb565(246, 246, 244)
#define C_LINE    face_rgb565(150, 42, 4)
#define C_INK     face_rgb565(18, 14, 14)
#define C_SHELL   face_rgb565(242, 86, 22)
#define C_SHADE   face_rgb565(204, 60, 10)
#define C_LIGHT   face_rgb565(250, 132, 72)
#define C_WHITE   face_rgb565(252, 250, 246)
#define C_RIM     face_rgb565(200, 30, 26)
#define C_TEETH   face_rgb565(232, 222, 170)
#define C_MOUTH   face_rgb565(60, 12, 12)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int clampi(int v, int lo, int hi)
{
  if (v < lo)
    {
      return lo;
    }

  if (v > hi)
    {
      return hi;
    }

  return v;
}

static void rect(const struct face_surface *s, int scale, int x, int y,
                 int w, int h, uint16_t colour)
{
  face_grid_rect(s, GRID, scale, x, y, w, h, colour);
}

static void ellipse(const struct face_surface *s, int scale, int cx, int cy,
                    int rx, int ry, uint16_t colour)
{
  face_grid_ellipse(s, GRID, scale, cx, cy, rx, ry, colour);
}

/* A limb: a straight run of thick by thick blocks from one cell to another.
 * Stepped along the longer axis, so it has no gaps at any slope.
 */

static void limb(const struct face_surface *s, int scale, int x0, int y0,
                 int x1, int y1, int thick, uint16_t colour)
{
  int dx = x1 - x0;
  int dy = y1 - y0;
  int adx = dx < 0 ? -dx : dx;
  int ady = dy < 0 ? -dy : dy;
  int steps = adx > ady ? adx : ady;
  int i;

  for (i = 0; i <= steps; i++)
    {
      int x = x0 + (steps ? (dx * i) / steps : 0);
      int y = y0 + (steps ? (dy * i) / steps : 0);

      rect(s, scale, x - thick / 2, y - thick / 2, thick, thick, colour);
    }
}

/* An outlined limb: the outline run first, then the orange one inside it. */

static void leg(const struct face_surface *s, int scale, int x0, int y0,
                int x1, int y1, int thick)
{
  limb(s, scale, x0, y0, x1, y1, thick + 2, C_LINE);
  limb(s, scale, x0, y0, x1, y1, thick, C_SHELL);
}

/* One claw with its arm, mirrored for the right side.  lift raises it,
 * which is what the crab does when it is angrier than usual.
 */

static void claw(const struct face_surface *s, int scale, int mirror,
                 int lift)
{
  int cx = mirror ? GRID - 1 - 7 : 7;
  int cy = 8 - lift;
  int shoulder = mirror ? GRID - 1 - 10 : 10;
  int i;

  /* The arm, from the shoulder of the shell up to the claw. */

  leg(s, scale, shoulder, 17, cx, cy + 5, 2);

  /* A mitten shaped claw: tall oval with a white zigzag for the gap between
   * the pincers, and a highlight on the outer side.
   */

  ellipse(s, scale, cx, cy, 6, 7, C_LINE);
  ellipse(s, scale, cx, cy, 5, 6, C_SHELL);
  ellipse(s, scale, mirror ? cx + 2 : cx - 2, cy - 2, 2, 3, C_LIGHT);

  for (i = -5; i <= 3; i++)
    {
      int zig = (i & 1) ? (mirror ? 1 : -1) : 0;

      rect(s, scale, cx + zig, cy + i, 1, 1, C_WHITE);
    }
}

static void eye(const struct face_surface *s, int scale, int x, int open,
                int px, int py)
{
  int lid = (4 * (FACE_UNIT - clampi(open, 0, FACE_UNIT))) / FACE_UNIT;

  /* Red rim, white, and a pupil that follows the gaze. */

  rect(s, scale, x, 18, 7, 4, C_RIM);
  rect(s, scale, x + 1, 18, 5, 4, C_WHITE);
  rect(s, scale, x + 2 + px, 19 + py, 2, 2, C_INK);

  if (lid > 0)
    {
      rect(s, scale, x, 18, 7, lid, C_SHELL);
      rect(s, scale, x, 17 + lid, 7, 1, C_INK);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_render_crab(const struct face_surface *s,
                      const struct face_pose *pose,
                      enum face_state state,
                      uint32_t now_ms,
                      int palette,
                      struct face_dirty *dirty)
{
  int scale = s->width / GRID;
  int brow = clampi(pose->brow - ANGER_BIAS, -FACE_UNIT, FACE_UNIT);
  int lift = clampi(-brow * 2 / FACE_UNIT, 0, 2);
  int open;
  int brow_y;
  int px;
  int py;
  int i;
  int dx;

  (void)state;
  (void)now_ms;
  (void)palette;

  if (scale < 1)
    {
      scale = 1;
    }

  face_sprite_clear(s, C_BG);

  /* Legs first, so the shell covers their roots.  Three a side, each in
   * two segments: out from the shell, then down.
   */

  leg(s, scale, 9, 27, 3, 30, 1);
  leg(s, scale, 3, 30, 1, 36, 1);
  leg(s, scale, 10, 30, 5, 34, 1);
  leg(s, scale, 5, 34, 4, 39, 1);
  leg(s, scale, 13, 33, 10, 36, 1);
  leg(s, scale, 10, 36, 9, 39, 1);

  leg(s, scale, 30, 27, 36, 30, 1);
  leg(s, scale, 36, 30, 38, 36, 1);
  leg(s, scale, 29, 30, 34, 34, 1);
  leg(s, scale, 34, 34, 35, 39, 1);
  leg(s, scale, 26, 33, 29, 36, 1);
  leg(s, scale, 29, 36, 30, 39, 1);

  claw(s, scale, 0, lift);
  claw(s, scale, 1, lift);

  /* The shell, which is also the face: a big round body. */

  ellipse(s, scale, 20, 23, 14, 12, C_LINE);
  ellipse(s, scale, 20, 23, 13, 11, C_SHELL);
  ellipse(s, scale, 20, 30, 10, 3, C_SHADE);
  ellipse(s, scale, 15, 13, 3, 1, C_LIGHT);

  /* Stubble along the chin and up the jaw. */

  rect(s, scale, 12, 29, 1, 1, C_INK);
  rect(s, scale, 13, 31, 1, 1, C_INK);
  rect(s, scale, 15, 32, 1, 1, C_INK);
  rect(s, scale, 17, 33, 1, 1, C_INK);
  rect(s, scale, 19, 33, 1, 1, C_INK);
  rect(s, scale, 21, 33, 1, 1, C_INK);
  rect(s, scale, 23, 33, 1, 1, C_INK);
  rect(s, scale, 25, 32, 1, 1, C_INK);
  rect(s, scale, 27, 31, 1, 1, C_INK);
  rect(s, scale, 28, 29, 1, 1, C_INK);
  rect(s, scale, 14, 30, 1, 1, C_SHADE);
  rect(s, scale, 16, 31, 1, 1, C_SHADE);
  rect(s, scale, 24, 31, 1, 1, C_SHADE);
  rect(s, scale, 26, 30, 1, 1, C_SHADE);

  /* Glasses: two thin frames, a bridge, and arms out to the shell's edge.
   * The eyes sit inside them.
   */

  rect(s, scale, 10, 17, 9, 6, C_INK);
  rect(s, scale, 11, 18, 7, 4, C_SHELL);
  rect(s, scale, 21, 17, 9, 6, C_INK);
  rect(s, scale, 22, 18, 7, 4, C_SHELL);
  rect(s, scale, 19, 19, 2, 1, C_INK);
  rect(s, scale, 7, 19, 3, 1, C_INK);
  rect(s, scale, 30, 19, 3, 1, C_INK);

  px = clampi(pose->pupil_x / (FACE_UNIT / 2 + 1), -1, 1);
  py = clampi(pose->pupil_y / (FACE_UNIT / 2 + 1), 0, 1);

  eye(s, scale, 11, pose->eye_open_l, px, py);
  eye(s, scale, 22, pose->eye_open_r, px, py);

  /* Brows: thick, and tilted by the biased brow.  The inner end drops as
   * the brow lowers, and y grows downwards.
   */

  brow_y = clampi(14 - (2 * brow) / FACE_UNIT, 12, 14);

  for (i = 0; i < 2; i++)
    {
      int inner = (i == 0) ? 1 : -1;
      int x0 = (i == 0) ? 10 : 22;
      int k;

      for (k = 0; k < 8; k++)
        {
          int tilt = -((2 * k - 7) * inner * brow) / (2 * FACE_UNIT);

          rect(s, scale, x0 + k, brow_y + tilt, 1, 2, C_INK);
        }
    }

  /* Creases between the brows when they are down. */

  if (brow < -FACE_UNIT / 2)
    {
      rect(s, scale, 18, 13, 1, 3, C_SHADE);
      rect(s, scale, 21, 13, 1, 3, C_SHADE);
    }

  /* Mouth.  Never quite shut, and a frown opens it further, so anger is a
   * shout with teeth rather than a pursed line.
   */

  open = pose->mouth_open + MOUTH_BIAS;
  if (pose->mouth_curve < 0)
    {
      open -= 2 * pose->mouth_curve;
    }

  open = clampi(open, 0, FACE_UNIT);

  for (dx = -6; dx <= 6; dx++)
    {
      int32_t norm  = ((int32_t)dx * FACE_UNIT) / 6;
      int32_t bulge = FACE_UNIT - (norm * norm) / FACE_UNIT;
      int yc = 28 + (int)(((int32_t)pose->mouth_curve * 2 * bulge)
                          / (FACE_UNIT * (int32_t)FACE_UNIT));
      int hc = (int)(((int32_t)open * 6 * (FACE_UNIT / 3 + 2 * bulge / 3))
                     / (FACE_UNIT * (int32_t)FACE_UNIT));
      int top;

      if (hc < 1)
        {
          hc = 1;
        }

      top = yc - hc / 2;
      rect(s, scale, 20 + dx, top - 1, 1, hc + 2, C_INK);

      if (hc >= 3)
        {
          rect(s, scale, 20 + dx, top, 1, hc, C_MOUTH);

          if (dx >= -4 && dx <= 4)
            {
              rect(s, scale, 20 + dx, top, 1, 1, C_TEETH);
            }
        }
    }

  if (dirty != NULL)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
    }
}
