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

/* Colours, fixed like the crab's, since the penguin is these colours. */

#define C_BG      face_rgb565(168, 214, 236)
#define C_LINE    face_rgb565(16, 16, 20)
#define C_WHITE   face_rgb565(250, 250, 250)
#define C_GREY    face_rgb565(126, 130, 136)
#define C_GREY_DK face_rgb565(92, 96, 102)
#define C_BEAK    face_rgb565(240, 168, 56)
#define C_BEAK_DK face_rgb565(120, 60, 20)
#define C_BROWN   face_rgb565(142, 84, 40)
#define C_BROWN_D face_rgb565(104, 60, 28)
#define C_STEEL   face_rgb565(176, 196, 206)
#define C_STEEL_D face_rgb565(132, 152, 164)
#define C_HORN    face_rgb565(226, 218, 182)
#define C_HORN_DK face_rgb565(176, 164, 124)

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

/* A horn: a run of blocks from the helmet up and out, thick at the base and
 * one cell wide at the tip, with a dark outline under it.
 */

static void horn(const struct face_surface *s, int scale, int x0, int y0,
                 int x1, int y1)
{
  int steps = y0 - y1;
  int i;

  for (i = 0; i <= steps; i++)
    {
      int x = x0 + ((x1 - x0) * i) / steps;
      int y = y0 - i;
      int thick = 3 - (2 * i) / steps;

      rect(s, scale, x - thick / 2 - 1, y - 1, thick + 2, 3, C_LINE);
    }

  for (i = 0; i <= steps; i++)
    {
      int x = x0 + ((x1 - x0) * i) / steps;
      int y = y0 - i;
      int thick = 3 - (2 * i) / steps;

      rect(s, scale, x - thick / 2, y, thick, 1, C_HORN);
      rect(s, scale, x - thick / 2, y, 1, 1, C_HORN_DK);
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
  int helmet;
  int open;
  int px;
  int py;
  int i;

  (void)state;
  (void)palette;

  if (scale < 1)
    {
      scale = 1;
    }

  /* The waddle: one cell left, centre, one cell right, centre. */

  sway = (int)((now_ms % SWAY_MS) * 4 / SWAY_MS);
  ox = (sway == 1) ? 1 : (sway == 3) ? -1 : 0;

  /* The helmet drops a cell when the brows lower and lifts when they rise,
   * which is the only brow this bird has.
   */

  helmet = pose->brow < -FACE_UNIT / 4 ? 1 : pose->brow > FACE_UNIT / 2 ? -1 : 0;

  face_sprite_clear(s, C_BG);

  /* Body: grey back on the left, white belly to the right, outlined. */

  ellipse(s, scale, 20 + ox, 37, 16, 15, C_LINE);
  ellipse(s, scale, 20 + ox, 37, 15, 14, C_GREY);
  ellipse(s, scale, 24 + ox, 38, 10, 12, C_WHITE);

  /* Head, with the grey of the back showing as a crescent on the left. */

  ellipse(s, scale, 20 + ox, 21, 12, 11, C_LINE);
  ellipse(s, scale, 20 + ox, 21, 11, 10, C_GREY);
  ellipse(s, scale, 22 + ox, 21, 9, 9, C_WHITE);

  /* Eyes and beak.  The gaze moves the glint, not the eye. */

  px = clampi(pose->pupil_x / (FACE_UNIT / 2 + 1), -1, 1);
  py = clampi(pose->pupil_y / (FACE_UNIT / 2 + 1), 0, 1);

  eye(s, scale, 15 + ox, 17, pose->eye_open_l, px, py);
  eye(s, scale, 24 + ox, 17, pose->eye_open_r, px, py);

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

  dome(s, scale, 20 + ox, 13 + helmet, 13, 9, C_LINE);
  dome(s, scale, 20 + ox, 13 + helmet, 12, 8, C_BROWN);
  dome(s, scale, 16 + ox, 13 + helmet, 6, 6, C_BROWN_D);
  dome(s, scale, 17 + ox, 13 + helmet, 5, 5, C_BROWN);

  rect(s, scale, 7 + ox, 11 + helmet, 26, 5, C_LINE);
  rect(s, scale, 8 + ox, 12 + helmet, 24, 3, C_STEEL);
  rect(s, scale, 8 + ox, 14 + helmet, 24, 1, C_STEEL_D);

  rect(s, scale, 18 + ox, 4 + helmet, 4, 9, C_LINE);
  rect(s, scale, 19 + ox, 5 + helmet, 2, 8, C_STEEL);

  for (i = 10; i <= 30; i += 4)
    {
      rect(s, scale, i + ox, 13 + helmet, 1, 1, C_GREY_DK);
    }

  rect(s, scale, 19 + ox, 7 + helmet, 1, 1, C_GREY_DK);
  rect(s, scale, 19 + ox, 10 + helmet, 1, 1, C_GREY_DK);

  horn(s, scale, 9 + ox, 10 + helmet, 4 + ox, 2 + helmet);
  horn(s, scale, 31 + ox, 10 + helmet, 36 + ox, 2 + helmet);

  /* Bow tie on a string round the neck. */

  rect(s, scale, 11 + ox, 30, 18, 1, C_LINE);
  rect(s, scale, 16 + ox, 29, 3, 4, C_LINE);
  rect(s, scale, 21 + ox, 29, 3, 4, C_LINE);
  rect(s, scale, 19 + ox, 30, 2, 2, C_LINE);
  rect(s, scale, 17 + ox, 30, 1, 1, C_GREY_DK);
  rect(s, scale, 22 + ox, 30, 1, 1, C_GREY_DK);

  if (dirty != NULL)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
    }
}
