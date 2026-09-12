/****************************************************************************
 * experiments/pico-face/src/face_penguin.c
 *
 * The penguin preset: a round penguin in a horned helmet and a bow tie,
 * drawn from outlined shapes on a 40 by 40 grid blown up to the panel.
 *
 * Animated from the pose and the clock.  The eyes blink and follow the
 * gaze, the beak opens with the mouth, the helmet slides down over the eyes
 * when the brows lower and lifts when they rise, and the whole bird sways
 * from side to side on a slow waddle.
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

/* One full sway, left to right and back, in milliseconds. */

#define SWAY_MS 2400

#define RGB(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | \
                                 (((g) & 0xfc) << 3) | \
                                 ((b) >> 3)))

struct penguin_colors
{
  uint16_t bg;
  uint16_t line;
  uint16_t white;
  uint16_t grey;
  uint16_t grey_dk;
  uint16_t wing;
  uint16_t beak;
  uint16_t beak_dk;
  uint16_t brown;
  uint16_t brown_d;
  uint16_t steel;
  uint16_t steel_d;
  uint16_t horn;
  uint16_t horn_dk;
};

static const struct penguin_colors g_penguin_palettes[FACE_NPALETTES] =
{
  /* 0: Arctic Day - Sky blue */
  {
    RGB(168, 214, 236),
    RGB(16, 16, 20),
    RGB(250, 250, 250),
    RGB(130, 134, 140),
    RGB(96, 100, 108),
    RGB(110, 114, 122),
    RGB(240, 168, 56),
    RGB(120, 60, 20),
    RGB(142, 84, 40),
    RGB(104, 60, 28),
    RGB(176, 196, 206),
    RGB(132, 152, 164),
    RGB(226, 218, 182),
    RGB(176, 164, 124)
  },
  /* 1: Polar Night - Starry navy */
  {
    RGB(16, 20, 36),
    RGB(8, 10, 18),
    RGB(240, 244, 250),
    RGB(100, 110, 130),
    RGB(70, 78, 96),
    RGB(85, 95, 115),
    RGB(250, 190, 60),
    RGB(140, 70, 20),
    RGB(90, 60, 50),
    RGB(65, 40, 35),
    RGB(130, 150, 180),
    RGB(90, 110, 140),
    RGB(210, 215, 200),
    RGB(150, 160, 150)
  },
  /* 2: Twilight / Sunset */
  {
    RGB(60, 30, 48),
    RGB(20, 12, 18),
    RGB(255, 235, 220),
    RGB(140, 90, 110),
    RGB(100, 60, 80),
    RGB(120, 75, 95),
    RGB(255, 140, 40),
    RGB(150, 50, 20),
    RGB(120, 50, 50),
    RGB(85, 35, 35),
    RGB(190, 140, 160),
    RGB(140, 95, 115),
    RGB(240, 200, 170),
    RGB(180, 140, 120)
  }
};

static const struct penguin_colors *g_penguin_colors = &g_penguin_palettes[0];

#define C_BG      (g_penguin_colors->bg)
#define C_LINE    (g_penguin_colors->line)
#define C_WHITE   (g_penguin_colors->white)
#define C_GREY    (g_penguin_colors->grey)
#define C_GREY_DK (g_penguin_colors->grey_dk)
#define C_WING    (g_penguin_colors->wing)
#define C_BEAK    (g_penguin_colors->beak)
#define C_BEAK_DK (g_penguin_colors->beak_dk)
#define C_BROWN   (g_penguin_colors->brown)
#define C_BROWN_D (g_penguin_colors->brown_d)
#define C_STEEL   (g_penguin_colors->steel)
#define C_STEEL_D (g_penguin_colors->steel_d)
#define C_HORN    (g_penguin_colors->horn)
#define C_HORN_DK (g_penguin_colors->horn_dk)

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

/* The upper half of an ellipse, for the helmet's dome. */

static void dome(const struct face_surface *s, int scale, int cx, int cy,
                 int rx, int ry, uint16_t colour)
{
  int x;
  int y;

  for (y = cy - ry; y <= cy; y++)
    {
      int dy = y - cy;

      for (x = cx - rx; x <= cx + rx; x++)
        {
          int dx = x - cx;

          if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
            {
              rect(s, scale, x, y, 1, 1, colour);
            }
        }
    }
}

/* A horn: one row of blocks per cell of height from the band up to the tip,
 * flaring out from the helmet and then curling back up and in, as the
 * drawing's do.  Thick at the base, one cell at the tip, outlined.  dir is
 * +1 for a horn that flares to the right and -1 for one that flares left.
 */

static void horn(const struct face_surface *s, int scale, int x0, int y0,
                 int height, int dir)
{
  int pass;

  for (pass = 0; pass < 2; pass++)
    {
      int i;

      for (i = 0; i <= height; i++)
        {
          /* Outward quickly at first, then back in towards the tip. */

          int out = (i * (2 * height - i)) / (height * 2 / 3 + 1) / 3;
          int back = (i > height * 2 / 3) ? (i - height * 2 / 3) : 0;
          int x = x0 + dir * (out - back);
          int y = y0 - i;
          int thick = 3 - (2 * i) / height;

          if (pass == 0)
            {
              rect(s, scale, x - thick / 2 - 1, y - 1, thick + 2, 3,
                   C_LINE);
            }
          else
            {
              rect(s, scale, x - thick / 2, y, thick, 1, C_HORN);
              rect(s, scale, x - thick / 2 + (dir > 0 ? thick - 1 : 0), y,
                   1, 1, C_HORN_DK);
            }
        }
    }
}

/* One eye: a dark oval with a white glint that follows the gaze, and a lid
 * of face colour that comes down as the eye closes.
 */

static void eye(const struct face_surface *s, int scale, int x, int y,
                int open, int px, int py)
{
  int lid = (4 * (FACE_UNIT - clampi(open, 0, FACE_UNIT))) / FACE_UNIT;

  /* A three by four oval with its corners knocked off. */

  rect(s, scale, x, y, 3, 4, C_LINE);
  rect(s, scale, x, y, 1, 1, C_WHITE);
  rect(s, scale, x + 2, y, 1, 1, C_WHITE);
  rect(s, scale, x, y + 3, 1, 1, C_WHITE);
  rect(s, scale, x + 2, y + 3, 1, 1, C_WHITE);

  rect(s, scale, x + 1 + px, y + 1 + py, 1, 1, C_WHITE);

  if (lid > 0)
    {
      rect(s, scale, x, y, 3, lid, C_WHITE);
      rect(s, scale, x, y + lid - 1, 3, 1, C_LINE);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_render_penguin(const struct face_surface *s,
                         const struct face_pose *pose,
                         enum face_state state,
                         uint32_t now_ms,
                         int palette,
                         struct face_dirty *dirty)
{
  int scale = s->width / GRID;
  int sway;
  int ox;
  int pal_idx = palette % FACE_NPALETTES;
  int helmet;
  int open;
  int px;
  int py;
  int i;
  int wing_y;

  if (pal_idx < 0)
    {
      pal_idx += FACE_NPALETTES;
    }

  g_penguin_colors = &g_penguin_palettes[pal_idx];

  if (scale < 1)
    {
      scale = 1;
    }

  /* The waddle: one cell left, centre, one cell right, centre. */

  sway = (int)((now_ms % SWAY_MS) * 4 / SWAY_MS);
  ox = (sway == 1) ? 1 : (sway == 3) ? -1 : 0;

  /* The helmet drops a cell when the brows lower and lifts when they rise,
   * which is the only brow this bird has.  On failed, it droops further down.
   */

  helmet = pose->brow < -FACE_UNIT / 4 ? 1 : pose->brow > FACE_UNIT / 2 ? -1 : 0;
  if (state == FACE_FAILED)
    {
      helmet += 2;
    }

  face_sprite_clear(s, C_BG);

  /* Body: grey back on the left, white belly to the right, outlined, with
   * the wing lying along the left side.
   */

  wing_y = 34 + (state == FACE_FAILED ? 2 : 0);

  ellipse(s, scale, 20 + ox, 37, 17, 15, C_LINE);
  ellipse(s, scale, 20 + ox, 37, 16, 14, C_GREY);
  ellipse(s, scale, 25 + ox, 38, 11, 12, C_WHITE);
  ellipse(s, scale, 8 + ox, wing_y, 5, 9, C_LINE);
  ellipse(s, scale, 8 + ox, wing_y, 4, 8, C_WING);

  /* Head, with the grey of the back showing as a crescent on the left. */

  ellipse(s, scale, 20 + ox, 21, 13, 11, C_LINE);
  ellipse(s, scale, 20 + ox, 21, 12, 10, C_GREY);
  ellipse(s, scale, 22 + ox, 21, 10, 9, C_WHITE);

  /* Eyes and beak.  The gaze moves the glint, not the eye. */

  px = clampi(pose->pupil_x / (FACE_UNIT / 2 + 1), -1, 1);
  py = clampi(pose->pupil_y / (FACE_UNIT / 2 + 1), 0, 1);

  eye(s, scale, 14 + ox, 17, pose->eye_open_l, px, py);
  eye(s, scale, 25 + ox, 17, pose->eye_open_r, px, py);

  open = (3 * clampi(pose->mouth_open, 0, FACE_UNIT)) / FACE_UNIT;

  rect(s, scale, 19 + ox, 21, 5, 3 + open, C_LINE);
  rect(s, scale, 20 + ox, 22, 3, 1, C_BEAK);

  if (open > 0)
    {
      rect(s, scale, 20 + ox, 23, 3, open, C_BEAK_DK);
    }

  rect(s, scale, 20 + ox, 23 + open, 3, 1, C_BEAK);
  rect(s, scale, 21 + ox, 22, 1, 1, C_WHITE);

  /* Helmet: brown dome over a riveted steel band, a strip down the middle,
   * and a horn each side.  Drawn after the head so it sits on top of it.
   */

  dome(s, scale, 20 + ox, 13 + helmet, 14, 9, C_LINE);
  dome(s, scale, 20 + ox, 13 + helmet, 13, 8, C_BROWN);
  dome(s, scale, 15 + ox, 13 + helmet, 6, 6, C_BROWN_D);
  dome(s, scale, 16 + ox, 13 + helmet, 5, 5, C_BROWN);

  rect(s, scale, 6 + ox, 11 + helmet, 28, 5, C_LINE);
  rect(s, scale, 7 + ox, 12 + helmet, 26, 3, C_STEEL);
  rect(s, scale, 7 + ox, 14 + helmet, 26, 1, C_STEEL_D);

  rect(s, scale, 18 + ox, 3 + helmet, 4, 10, C_LINE);
  rect(s, scale, 19 + ox, 4 + helmet, 2, 9, C_STEEL);

  /* Rivets: pale discs along the band and down the strip. */

  for (i = 9; i <= 31; i += 4)
    {
      rect(s, scale, i + ox, 13 + helmet, 1, 1, C_WHITE);
      rect(s, scale, i + ox, 14 + helmet, 1, 1, C_GREY_DK);
    }

  rect(s, scale, 19 + ox, 6 + helmet, 1, 1, C_WHITE);
  rect(s, scale, 19 + ox, 9 + helmet, 1, 1, C_WHITE);

  horn(s, scale, 8 + ox, 11 + helmet, 9, -1);
  horn(s, scale, 32 + ox, 11 + helmet, 9, 1);

  /* Bow tie on a string that hangs lower in the middle of the neck. */

  for (i = -9; i <= 9; i++)
    {
      rect(s, scale, 20 + ox + i, 27 + (81 - i * i) / 30, 1, 1, C_LINE);
    }

  rect(s, scale, 15 + ox, 28, 3, 4, C_LINE);
  rect(s, scale, 22 + ox, 28, 3, 4, C_LINE);
  rect(s, scale, 18 + ox, 29, 4, 2, C_LINE);
  rect(s, scale, 16 + ox, 29, 1, 1, C_GREY_DK);
  rect(s, scale, 23 + ox, 29, 1, 1, C_GREY_DK);

  if (dirty != NULL)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
    }
}
