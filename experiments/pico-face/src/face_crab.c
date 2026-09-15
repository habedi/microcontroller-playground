/****************************************************************************
 * experiments/pico-face/src/face_crab.c
 *
 * Crab preset: shapes on a 40 by 40 grid.
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

#define RGB(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | \
                                 (((g) & 0xfc) << 3) | \
                                 ((b) >> 3)))

struct crab_colors
{
  uint16_t bg;
  uint16_t line;
  uint16_t ink;
  uint16_t shell;
  uint16_t shade;
  uint16_t light;
  uint16_t white;
  uint16_t rim;
  uint16_t teeth;
  uint16_t mouth;
};

static const struct crab_colors g_crab_palettes[FACE_NPALETTES] =
{
  /* 0: Day */
  {
    RGB(246, 246, 244),
    RGB(150, 42, 4),
    RGB(18, 14, 14),
    RGB(242, 86, 22),
    RGB(204, 60, 10),
    RGB(250, 132, 72),
    RGB(252, 250, 246),
    RGB(200, 30, 26),
    RGB(232, 222, 170),
    RGB(60, 12, 12)
  },
  /* 1: Night */
  {
    RGB(18, 22, 32),
    RGB(14, 68, 120),
    RGB(10, 12, 18),
    RGB(32, 140, 220),
    RGB(20, 100, 170),
    RGB(90, 190, 255),
    RGB(230, 245, 255),
    RGB(20, 90, 180),
    RGB(180, 220, 240),
    RGB(12, 24, 48)
  },
  /* 2: Retro */
  {
    RGB(12, 20, 12),
    RGB(20, 100, 30),
    RGB(8, 14, 8),
    RGB(40, 200, 70),
    RGB(24, 140, 48),
    RGB(110, 245, 140),
    RGB(220, 255, 225),
    RGB(30, 160, 60),
    RGB(170, 240, 180),
    RGB(10, 40, 16)
  }
};

static const struct crab_colors *g_crab_colors = &g_crab_palettes[0];

#define C_BG      (g_crab_colors->bg)
#define C_LINE    (g_crab_colors->line)
#define C_INK     (g_crab_colors->ink)
#define C_SHELL   (g_crab_colors->shell)
#define C_SHADE   (g_crab_colors->shade)
#define C_LIGHT   (g_crab_colors->light)
#define C_WHITE   (g_crab_colors->white)
#define C_RIM     (g_crab_colors->rim)
#define C_TEETH   (g_crab_colors->teeth)
#define C_MOUTH   (g_crab_colors->mouth)

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
  int pal_idx = palette % FACE_NPALETTES;
  int typing_l = 0;
  int typing_r = 0;
  int open;
  int brow_y;
  int px;
  int py;
  int i;
  int dx;

  if (pal_idx < 0)
    {
      pal_idx += FACE_NPALETTES;
    }

  g_crab_colors = &g_crab_palettes[pal_idx];

  if (state == FACE_WORKING)
    {
      int step = (now_ms / 140) & 1;
      typing_l = step ? 2 : 0;
      typing_r = step ? 0 : 2;
    }
  else if (state == FACE_FAILED)
    {
      typing_l = 2;
      typing_r = 2;
    }

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

  claw(s, scale, 0, lift + typing_l);
  claw(s, scale, 1, lift + typing_r);

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

  /* Weathered barnacle markings on upper carapace */

  rect(s, scale, 27, 13, 2, 1, C_LIGHT);
  rect(s, scale, 28, 14, 1, 1, C_SHADE);

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

  /* Glint on glasses corner */

  rect(s, scale, 11, 18, 1, 1, C_WHITE);
  rect(s, scale, 22, 18, 1, 1, C_WHITE);

  /* Steam puffs from shell vents on failure */

  if (state == FACE_FAILED)
    {
      int puff = (int)((now_ms / 150) % 3);

      rect(s, scale, 3 - puff, 15 - puff, 2, 2, C_WHITE);
      rect(s, scale, 36 + puff, 15 - puff, 2, 2, C_WHITE);
      rect(s, scale, 4, 16, 1, 1, C_LIGHT);
      rect(s, scale, 35, 16, 1, 1, C_LIGHT);
    }

  /* Editing stylus in right claw */

  if (state == FACE_EDITING)
    {
      rect(s, scale, 33, 3, 1, 4, C_TEETH);
      rect(s, scale, 33, 1, 1, 2, C_WHITE);
    }

  /* Victory sparkles and rosy cheeks on done */

  if (state == FACE_DONE)
    {
      rect(s, scale, 7, 1, 1, 3, C_WHITE);
      rect(s, scale, 6, 2, 3, 1, C_WHITE);
      rect(s, scale, 32, 1, 1, 3, C_WHITE);
      rect(s, scale, 31, 2, 3, 1, C_WHITE);

      rect(s, scale, 8, 23, 2, 1, C_LIGHT);
      rect(s, scale, 30, 23, 2, 1, C_LIGHT);
    }

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
