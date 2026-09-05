/****************************************************************************
 * experiments/pico-face/src/face_crab.c
 *
 * The crab preset: an angry crustacean whose shell is its face, after the
 * well known drawing.  Drawn from shapes on a 40 by 40 grid blown up to the
 * panel, with a black outline under every orange shape, so it reads as
 * outlined pixel art rather than as smooth ellipses.
 *
 * The face takes its expression from the pose like the other drawn presets,
 * with one twist: the brows carry an anger bias, so the crab is grumpy at
 * rest and furious when a tool fails, and a frown opens its mouth into a
 * shout.
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

/* How much angrier than the pose the crab always is, on the brow scale. */

#define ANGER_BIAS 500

/* Colours.  The crab keeps its own rather than using the shared palettes,
 * because a purple crab is not the crab in the drawing.
 */

#define C_BG      face_rgb565(14, 8, 6)
#define C_LINE    face_rgb565(12, 8, 8)
#define C_SHELL   face_rgb565(238, 78, 18)
#define C_SHADE   face_rgb565(186, 48, 10)
#define C_LIGHT   face_rgb565(252, 124, 60)
#define C_WHITE   face_rgb565(245, 242, 235)
#define C_RIM     face_rgb565(196, 30, 26)
#define C_TEETH   face_rgb565(236, 226, 176)
#define C_MOUTH   face_rgb565(56, 10, 10)

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
  int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
              ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
  int i;

  for (i = 0; i <= steps; i++)
    {
      int x = x0 + (steps ? (dx * i) / steps : 0);
      int y = y0 + (steps ? (dy * i) / steps : 0);

      rect(s, scale, x - thick / 2, y - thick / 2, thick, thick, colour);
    }
}

/* An outlined limb: the black run first, then the orange one inside it. */

static void leg(const struct face_surface *s, int scale, int x0, int y0,
                int x1, int y1)
{
  limb(s, scale, x0, y0, x1, y1, 3, C_LINE);
  limb(s, scale, x0, y0, x1, y1, 1, C_SHELL);
}

/* One claw with its arm, mirrored for the right side.  lift raises it, which
 * is what the crab does when it is angrier than usual.
 */

static void claw(const struct face_surface *s, int scale, int mirror,
                 int lift)
{
  int cx = mirror ? GRID - 1 - 8 : 8;
  int cy = 9 - lift;
  int zig;
  int i;

  /* Arm from the shell's shoulder up to the claw. */

  limb(s, scale, mirror ? GRID - 1 - 12 : 12, 20, cx, cy + 5, 3, C_LINE);
  limb(s, scale, mirror ? GRID - 1 - 12 : 12, 20, cx, cy + 5, 1, C_SHELL);

  ellipse(s, scale, cx, cy, 5, 6, C_LINE);
  ellipse(s, scale, cx, cy, 4, 5, C_SHELL);
  ellipse(s, scale, cx - 1, cy - 2, 2, 2, C_LIGHT);

  /* The pincer's gap, a white zigzag down the middle. */

  for (i = -4; i <= 2; i++)
    {
      zig = (i & 1) ? -1 : 0;
      rect(s, scale, cx + zig, cy + i, 1, 1, C_WHITE);
    }
}

static void eye(const struct face_surface *s, int scale, int x, int open,
                int px, int py)
{
  /* Rows of lid, rounded down so a glare at 280 still shows two rows of
   * white and only a real blink shuts the eye.
   */

  int lid = (4 * (FACE_UNIT - clampi(open, 0, FACE_UNIT))) / FACE_UNIT;

  /* Red rim, white, and a pupil that follows the gaze. */

  rect(s, scale, x, 18, 6, 4, C_RIM);
  rect(s, scale, x + 1, 18, 4, 4, C_WHITE);
  rect(s, scale, x + 2 + px, 19 + py, 2, 2, C_LINE);

  if (lid > 0)
    {
      rect(s, scale, x, 18, 6, lid, C_SHELL);
      rect(s, scale, x, 17 + lid, 6, 1, C_LINE);
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

  /* The crab has its own colours and takes its shape from the pose, so
   * the state, the clock, and the palette are not needed here.
   */

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

  leg(s, scale, 10, 26, 4, 29);
  leg(s, scale, 4, 29, 2, 35);
  leg(s, scale, 11, 29, 6, 33);
  leg(s, scale, 6, 33, 5, 38);
  leg(s, scale, 13, 31, 10, 35);
  leg(s, scale, 10, 35, 9, 39);

  leg(s, scale, 29, 26, 35, 29);
  leg(s, scale, 35, 29, 37, 35);
  leg(s, scale, 28, 29, 33, 33);
  leg(s, scale, 33, 33, 34, 38);
  leg(s, scale, 26, 31, 29, 35);
  leg(s, scale, 29, 35, 30, 39);

  claw(s, scale, 0, lift);
  claw(s, scale, 1, lift);

  /* The shell, which is also the face. */

  ellipse(s, scale, 20, 23, 13, 11, C_LINE);
  ellipse(s, scale, 20, 23, 12, 10, C_SHELL);
  ellipse(s, scale, 20, 29, 9, 3, C_SHADE);
  ellipse(s, scale, 15, 13, 3, 1, C_LIGHT);

  /* Stubble along the chin. */

  rect(s, scale, 14, 30, 1, 1, C_LINE);
  rect(s, scale, 16, 31, 1, 1, C_LINE);
  rect(s, scale, 18, 32, 1, 1, C_LINE);
  rect(s, scale, 20, 32, 1, 1, C_LINE);
  rect(s, scale, 22, 32, 1, 1, C_LINE);
  rect(s, scale, 24, 31, 1, 1, C_LINE);
  rect(s, scale, 26, 30, 1, 1, C_LINE);

  /* Glasses: two frames, a bridge, and arms out to the shell's edge. */

  rect(s, scale, 10, 16, 9, 7, C_LINE);
  rect(s, scale, 11, 17, 7, 5, C_SHELL);
  rect(s, scale, 21, 16, 9, 7, C_LINE);
  rect(s, scale, 22, 17, 7, 5, C_SHELL);
  rect(s, scale, 19, 19, 2, 1, C_LINE);
  rect(s, scale, 8, 18, 2, 1, C_LINE);
  rect(s, scale, 30, 18, 2, 1, C_LINE);

  px = clampi(pose->pupil_x / (FACE_UNIT / 2 + 1), -1, 1);
  py = clampi(pose->pupil_y / (FACE_UNIT / 2 + 1), 0, 1);

  eye(s, scale, 11, pose->eye_open_l, px, py);
  eye(s, scale, 22, pose->eye_open_r, px, py);

  /* Brows above the frames, tilted by the biased brow.  The inner end drops
   * as the brow lowers, and y grows downwards.
   */

  brow_y = clampi(15 - (2 * brow) / FACE_UNIT, 13, 15);

  for (i = 0; i < 2; i++)
    {
      int inner = (i == 0) ? 1 : -1;
      int x0 = (i == 0) ? 11 : 22;
      int k;

      for (k = 0; k < 7; k++)
        {
          int tilt = -((k - 3) * inner * brow * 2) / (3 * FACE_UNIT);

          rect(s, scale, x0 + k, brow_y + tilt, 1, 2, C_LINE);
        }
    }

  /* A crease between the brows when they are down. */

  if (brow < -FACE_UNIT / 2)
    {
      rect(s, scale, 19, 15, 1, 2, C_SHADE);
      rect(s, scale, 20, 15, 1, 2, C_SHADE);
    }

  /* Mouth.  A frown also opens it, twice over, so anger is a shout with
   * teeth rather than a pursed line.
   */

  open = pose->mouth_open;
  if (pose->mouth_curve < 0)
    {
      open -= 2 * pose->mouth_curve;
    }

  open = clampi(open, 0, FACE_UNIT);

  for (dx = -5; dx <= 5; dx++)
    {
      int32_t norm  = ((int32_t)dx * FACE_UNIT) / 5;
      int32_t bulge = FACE_UNIT - (norm * norm) / FACE_UNIT;
      int yc = 28 + (int)(((int32_t)pose->mouth_curve * 2 * bulge)
                          / (FACE_UNIT * (int32_t)FACE_UNIT));
      int hc = (int)(((int32_t)open * 5 * (FACE_UNIT / 2 + bulge / 2))
                     / (FACE_UNIT * (int32_t)FACE_UNIT));
      int top;

      if (hc < 1)
        {
          hc = 1;
        }

      top = yc - hc / 2;
      rect(s, scale, 20 + dx, top - 1, 1, hc + 2, C_LINE);

      if (hc >= 3)
        {
          rect(s, scale, 20 + dx, top, 1, hc, C_MOUTH);

          if (dx >= -3 && dx <= 3)
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
