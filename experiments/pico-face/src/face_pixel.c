/****************************************************************************
 * experiments/pico-face/src/face_pixel.c
 *
 * The pixel portrait preset.  Draws the same pose the vector preset draws,
 * but as a character bust on a 48 by 48 grid blown up to the panel, so every
 * edge lands on a pixel boundary and the result reads as pixel art rather
 * than as a smooth shape that happens to be chunky.
 *
 * There is no scratch buffer.  Each art pixel is written straight to the
 * panel as a block, which keeps this off the 4 KB task stack.
 *
 ****************************************************************************/

#include <stddef.h>

#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The art grid.  240 divides by 48 exactly, giving a 5 pixel block. */

#define GRID 48

/* Palette roles this preset uses, by index into struct face_palette. */

#define C_BG       0
#define C_OUTLINE  1
#define C_SKIN     2
#define C_SKIN_MID 3
#define C_SKIN_DK  4
#define C_HAIR     5
#define C_HAIR_DK  6
#define C_CLOTH    7
#define C_CLOTH_MD 8
#define C_CLOTH_DK 9
#define C_EYE      15
#define C_WHITE    14

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

/* A filled rectangle in art coordinates. */

static void px_rect(const struct face_surface *s, int scale,
                    int x0, int y0, int w, int h, uint16_t colour)
{
  int x;
  int y;

  for (y = y0; y < y0 + h; y++)
    {
      if (y < 0 || y >= GRID)
        {
          continue;
        }

      for (x = x0; x < x0 + w; x++)
        {
          if (x >= 0 && x < GRID)
            {
              face_sprite_block(s, x, y, scale, 0, 0, colour);
            }
        }
    }
}

/* A filled ellipse in art coordinates, centred on cx, cy.  Integer only, so
 * the boundary is decided by a squared distance test rather than a square
 * root, and the result is deliberately blocky.
 */

static void px_ellipse(const struct face_surface *s, int scale,
                       int cx, int cy, int rx, int ry, uint16_t colour)
{
  int x;
  int y;

  if (rx <= 0 || ry <= 0)
    {
      return;
    }

  for (y = cy - ry; y <= cy + ry; y++)
    {
      int dy = y - cy;

      if (y < 0 || y >= GRID)
        {
          continue;
        }

      for (x = cx - rx; x <= cx + rx; x++)
        {
          int dx = x - cx;

          if (x < 0 || x >= GRID)
            {
              continue;
            }

          /* dx^2 / rx^2 + dy^2 / ry^2 <= 1, multiplied out to stay whole. */

          if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
            {
              face_sprite_block(s, x, y, scale, 0, 0, colour);
            }
        }
    }
}

/* One eye.  A shut eye is drawn as a lash line rather than as lids closing
 * over the white, because at five pixels tall the covered version reads as a
 * blindfold instead of as a blink.
 */

static void px_eye(const struct face_surface *s, int scale,
                   const struct face_palette *pal,
                   int cx, int cy, int open, int px, int py)
{
  int lid;

  if (open < FACE_UNIT / 5)
    {
      /* Shut.  A dark line with a slight droop at the outer end. */

      px_rect(s, scale, cx - 4, cy, 9, 2, pal->colour[C_OUTLINE]);
      px_rect(s, scale, cx - 5, cy + 1, 1, 1, pal->colour[C_OUTLINE]);
      px_rect(s, scale, cx + 4, cy + 1, 1, 1, pal->colour[C_OUTLINE]);
      return;
    }

  /* Built from rectangles rather than ellipses.  An ellipse this small
   * degenerates into a diamond with spikes on the axes, which reads as an
   * artefact; a blocked out eye is also the more idiomatic pixel art shape.
   * The corners are knocked off by hand to take the hard edge away.
   */

  px_rect(s, scale, cx - 4, cy - 3, 9, 7, pal->colour[C_OUTLINE]);
  px_rect(s, scale, cx - 4, cy - 3, 1, 1, pal->colour[C_SKIN]);
  px_rect(s, scale, cx + 4, cy - 3, 1, 1, pal->colour[C_SKIN]);
  px_rect(s, scale, cx - 4, cy + 3, 1, 1, pal->colour[C_SKIN]);
  px_rect(s, scale, cx + 4, cy + 3, 1, 1, pal->colour[C_SKIN]);

  px_rect(s, scale, cx - 3, cy - 2, 7, 5, pal->colour[C_WHITE]);

  px_rect(s, scale, cx - 1 + px, cy - 1 + py, 3, 3, pal->colour[C_EYE]);
  px_rect(s, scale, cx + px, cy + py, 1, 1, pal->colour[C_OUTLINE]);

  /* Upper lid comes down as the eye narrows. */

  lid = 4 - (4 * clampi(open, 0, FACE_UNIT)) / FACE_UNIT;

  if (lid > 0)
    {
      px_rect(s, scale, cx - 5, cy - 4, 11, lid, pal->colour[C_SKIN]);
      px_rect(s, scale, cx - 4, cy - 5 + lid, 9, 1, pal->colour[C_OUTLINE]);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_render_pixel(const struct face_surface *s,
                       const struct face_pose *pose,
                       enum face_state state,
                       uint32_t now_ms,
                       int palette,
                       struct face_dirty *dirty)
{
  const struct face_palette *pal = face_palette(palette);
  int scale = s->width / GRID;
  int eye_cx[2];
  int brow_y;
  int px;
  int py;
  int i;
  int dx;

  /* The state and the clock drive the hero preset, not this one.  The pose
   * already carries everything this look needs.
   */

  (void)state;
  (void)now_ms;

  if (scale < 1)
    {
      scale = 1;
    }

  face_sprite_clear(s, pal->colour[C_BG]);

  /* Shoulders first, so the head overlaps them at the neck. */

  px_rect(s, scale, 6, 40, 36, 8, pal->colour[C_CLOTH_DK]);
  px_rect(s, scale, 8, 41, 32, 7, pal->colour[C_CLOTH_MD]);
  px_rect(s, scale, 12, 42, 24, 6, pal->colour[C_CLOTH]);
  px_rect(s, scale, 20, 38, 8, 4, pal->colour[C_SKIN_DK]);

  /* Hair behind, then the face over it, which leaves the hair showing as a
   * fringe along the top and sides.
   */

  px_ellipse(s, scale, 24, 21, 16, 17, pal->colour[C_HAIR_DK]);
  px_ellipse(s, scale, 24, 19, 15, 14, pal->colour[C_HAIR]);
  px_ellipse(s, scale, 24, 24, 13, 15, pal->colour[C_SKIN_DK]);
  px_ellipse(s, scale, 24, 25, 12, 14, pal->colour[C_SKIN]);

  /* Ears. */

  px_rect(s, scale, 10, 24, 3, 5, pal->colour[C_SKIN_MID]);
  px_rect(s, scale, 35, 24, 3, 5, pal->colour[C_SKIN_MID]);

  eye_cx[0] = 18;
  eye_cx[1] = 30;

  px = clampi((2 * pose->pupil_x) / FACE_UNIT, -2, 2);
  py = clampi((2 * pose->pupil_y) / FACE_UNIT, -2, 2);

  px_eye(s, scale, pal, eye_cx[0], 25, pose->eye_open_l, px, py);
  px_eye(s, scale, pal, eye_cx[1], 25, pose->eye_open_r, px, py);

  /* Brows.  Kept clear of each other so they never merge into one bar, and
   * tilted by the pose, which is most of the expression at this size.
   */

  brow_y = clampi(19 - (3 * pose->brow) / FACE_UNIT, 15, 22);

  for (i = 0; i < 2; i++)
    {
      /* Angry lowers the inner end, surprised raises it.  The inner end is
       * the one nearer the middle of the face, so the sign flips per side.
       */

      int inner = (i == 0) ? 1 : -1;
      int k;

      for (k = 0; k < 7; k++)
        {
          int bx = eye_cx[i] - 3 + k;
          int tilt = ((k - 3) * inner * pose->brow) / FACE_UNIT;

          px_rect(s, scale, bx, brow_y + tilt, 1, 2, pal->colour[C_HAIR_DK]);
        }
    }

  /* Mouth.  A parabola sampled on the grid, opening with mouth_open. */

  for (dx = -6; dx <= 6; dx++)
    {
      int32_t norm  = ((int32_t)dx * FACE_UNIT) / 6;
      int32_t bulge = FACE_UNIT - (norm * norm) / FACE_UNIT;
      int y = 34 + (int)(((int32_t)-pose->mouth_curve * 4 * bulge)
                         / (FACE_UNIT * (int32_t)FACE_UNIT));
      int thick = 2 + (int)(((int32_t)pose->mouth_open * 4 * bulge)
                            / (FACE_UNIT * (int32_t)FACE_UNIT));

      px_rect(s, scale, 24 + dx, y, 1, thick, pal->colour[C_OUTLINE]);
    }

  if (dirty != NULL)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
    }
}
