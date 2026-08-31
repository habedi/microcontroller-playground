/****************************************************************************
 * experiments/pico-face/src/face_render.c
 *
 * Integer only RGB565 drawing for the face.  Shapes are rounded rectangles
 * and parabolic bands rather than bitmaps, so an expression costs a handful
 * of numbers instead of a 115 KB image, and blending between two expressions
 * is just interpolation.
 *
 ****************************************************************************/

#include <string.h>

#include "face_render.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Layout on a 240x240 panel.  Everything scales off the surface size, so a
 * different panel still draws a sensible face.
 */

#define EYE_CX_L_PCT   32   /* eye centres, as a percentage of the width */
#define EYE_CX_R_PCT   68
#define EYE_CY_PCT     40
#define EYE_W_PCT      24
#define EYE_H_PCT      27
#define PUPIL_R_PCT     5
#define BROW_W_PCT     24
#define BROW_H_PCT      4
#define BROW_GAP_PCT   10   /* resting gap above the eye */
#define MOUTH_CY_PCT   74
#define MOUTH_W_PCT    30
#define MOUTH_H_PCT     4

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int pct(int of, int percent)
{
  return (of * percent) / 100;
}

static uint32_t isqrt32(uint32_t n)
{
  uint32_t x = n;
  uint32_t y = 0;
  uint32_t bit = 1u << 30;

  while (bit > x)
    {
      bit >>= 2;
    }

  while (bit != 0)
    {
      if (x >= y + bit)
        {
          x -= y + bit;
          y = (y >> 1) + bit;
        }
      else
        {
          y >>= 1;
        }

      bit >>= 2;
    }

  return y;
}

static uint16_t rgb565(int r, int g, int b)
{
  if (r < 0) r = 0; else if (r > 255) r = 255;
  if (g < 0) g = 0; else if (g > 255) g = 255;
  if (b < 0) b = 0; else if (b > 255) b = 255;

  return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

/* Fills one horizontal run, clipped to the surface.  Every shape below goes
 * through here, which is why none of them can write out of bounds.
 */

static void span(const struct face_surface *s, int y, int x0, int x1,
                 uint16_t colour)
{
  uint16_t *row;
  int x;

  if (y < 0 || y >= s->height)
    {
      return;
    }

  if (x0 < 0)
    {
      x0 = 0;
    }

  if (x1 > s->width - 1)
    {
      x1 = s->width - 1;
    }

  row = s->pixels + (size_t)y * (size_t)s->stride_px;

  for (x = x0; x <= x1; x++)
    {
      row[x] = colour;
    }
}

/* Half width of a rounded rectangle at a row dy above or below its centre.
 * Returns -1 for a row outside the shape.  Both the eye and the clipping of
 * the pupil inside it go through this, so the two can never disagree about
 * where the eye ends.
 */

static int round_rect_half(int half_w, int half_h, int r, int dy)
{
  int adist = dy < 0 ? -dy : dy;
  int over;

  if (adist > half_h || half_w <= 0 || half_h < 0)
    {
      return -1;
    }

  if (r > half_w)
    {
      r = half_w;
    }

  if (r > half_h)
    {
      r = half_h;
    }

  /* Distance into the corner circle along the short axis. */

  over = r - (half_h - adist);

  if (over <= 0)
    {
      return half_w;
    }

  /* Pull the row in by the circle's sagitta at that offset.  Measuring it
   * from the other side inverts the shape and draws an hourglass.
   */

  return half_w - (r - (int)isqrt32((uint32_t)(r * r - over * over)));
}

/* Rounded rectangle centred on cx, cy.  The corner radius is clamped so a
 * flattened rectangle becomes a capsule rather than breaking up, which is
 * what makes a blink look like a blink.
 */

static void round_rect(const struct face_surface *s, int cx, int cy,
                       int w, int h, int r, uint16_t colour)
{
  int half_w = w / 2;
  int half_h = h / 2;
  int dy;

  if (h <= 0 || w <= 0)
    {
      return;
    }

  for (dy = -half_h; dy <= half_h; dy++)
    {
      int hw = round_rect_half(half_w, half_h, r, dy);

      if (hw >= 0)
        {
          span(s, cy + dy, cx - hw, cx + hw, colour);
        }
    }
}

/* Filled circle clipped to a rounded rectangle, which is how the pupil is
 * kept inside the eyelid instead of hanging off it.
 */

static void circle_in_round_rect(const struct face_surface *s,
                                 int cx, int cy, int half_w, int half_h,
                                 int r, int px, int py, int pr,
                                 uint16_t colour)
{
  int dy;

  for (dy = -pr; dy <= pr; dy++)
    {
      int y = py + dy;
      int dx = (int)isqrt32((uint32_t)(pr * pr - dy * dy));
      int hw = round_rect_half(half_w, half_h, r, y - cy);
      int x0;
      int x1;

      if (hw < 0)
        {
          continue;
        }

      x0 = px - dx;
      x1 = px + dx;

      if (x0 < cx - hw)
        {
          x0 = cx - hw;
        }

      if (x1 > cx + hw)
        {
          x1 = cx + hw;
        }

      if (x0 <= x1)
        {
          span(s, y, x0, x1, colour);
        }
    }
}

/* A parabolic band, used for the mouth.  curve is the vertical offset of the
 * middle relative to the ends, so a positive value smiles.
 */

static void parabola(const struct face_surface *s, int cx, int cy, int w,
                     int thickness, int curve, uint16_t colour)
{
  int half_w = w / 2;
  int dx;

  if (half_w <= 0 || thickness <= 0)
    {
      return;
    }

  for (dx = -half_w; dx <= half_w; dx++)
    {
      /* 1 - (dx / half_w)^2, in thousandths, then scaled by the curve. */

      int32_t norm = (int32_t)dx * FACE_UNIT / half_w;
      int32_t bulge = FACE_UNIT - (norm * norm) / FACE_UNIT;
      int y = cy + (int)((int32_t)curve * bulge / FACE_UNIT);
      int t;

      /* Taper towards the corners.  A band of constant thickness following a
       * shallow curve stair-steps badly at this resolution, and a stroke that
       * thins at the ends hides it and looks deliberate.
       */

      int thick = (int)((int32_t)thickness
                        * (FACE_UNIT / 3 + (2 * bulge) / 3) / FACE_UNIT);

      if (thick < 1)
        {
          thick = 1;
        }

      for (t = 0; t < thick; t++)
        {
          span(s, y - thick / 2 + t, cx + dx, cx + dx, colour);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_render(const struct face_surface *s, const struct face_pose *pose,
                 struct face_dirty *dirty)
{
  int eye_w   = pct(s->width, EYE_W_PCT);
  int eye_h_max = pct(s->height, EYE_H_PCT);
  int eye_cy  = pct(s->height, EYE_CY_PCT);
  int pupil_r = pct(s->width, PUPIL_R_PCT);
  int cx[2];
  int eye_h[2];
  int pr;
  int i;

  /* Palette.  A warm amber on near black, brightened by the glow, which is
   * the only thing that changes colour rather than shape.
   */

  int lift = (pose->glow * 90) / FACE_UNIT;
  uint16_t bg    = rgb565(6 + lift / 6, 5 + lift / 8, 8 + lift / 10);
  uint16_t iris  = rgb565(150 + lift, 90 + lift, 40 + lift / 2);
  uint16_t pupil = rgb565(20, 14, 12);
  uint16_t trim  = rgb565(120 + lift, 70 + lift, 32 + lift / 2);

  cx[0] = pct(s->width, EYE_CX_L_PCT);
  cx[1] = pct(s->width, EYE_CX_R_PCT);
  eye_h[0] = (eye_h_max * pose->eye_open_l) / FACE_UNIT;
  eye_h[1] = (eye_h_max * pose->eye_open_r) / FACE_UNIT;

  /* Clear.  A full clear every frame is 115 KB of stores, which the M33 does
   * in well under a millisecond, so there is no reason to be cleverer.
   */

  for (i = 0; i < s->height; i++)
    {
      span(s, i, 0, s->width - 1, bg);
    }

  for (i = 0; i < 2; i++)
    {
      /* Brow above the eye, dropping as brow goes negative. */

      int brow_gap = pct(s->height, BROW_GAP_PCT)
                     + (pose->brow * pct(s->height, 6)) / FACE_UNIT;
      int brow_h = pct(s->height, BROW_H_PCT);

      round_rect(s, cx[i], eye_cy - eye_h_max / 2 - brow_gap,
                 pct(s->width, BROW_W_PCT), brow_h, brow_h / 2, trim);

      if (eye_h[i] <= 2)
        {
          /* Shut.  A thin line reads as a closed lid, where a zero height
           * rounded rectangle would just vanish.
           */

          round_rect(s, cx[i], eye_cy, eye_w, 3, 1, trim);
          continue;
        }

      round_rect(s, cx[i], eye_cy, eye_w, eye_h[i], eye_w / 2, iris);

      /* The pupil rides inside the eye and is clipped by its height, so it
       * disappears as the lid closes instead of floating over it.
       */

      /* Shrink the pupil to fit the lid gap.  At full size it would spill
       * over a narrowed eye and read as a dark blob rather than an eye.
       */

      pr = pupil_r;
      if (pr > eye_h[i] / 2 - 2)
        {
          pr = eye_h[i] / 2 - 2;
        }

      /* Below this the pupil is a speck on a nearly shut eye, which reads as
       * dirt rather than an eye.  A closed lid looks better with none.
       */

      if (pr > 3)
        {
          /* Stop a full pupil short of the lid so the gaze never has to be
           * clipped in normal use.  circle_in_round_rect() still clips, but
           * only as a guard against a future expression overreaching.
           */

          int room_x = (eye_w / 2) - pr;
          int room_y = (eye_h[i] / 2) - pr;
          int px = cx[i] + (pose->pupil_x * room_x) / FACE_UNIT;
          int py = eye_cy + (pose->pupil_y * room_y) / FACE_UNIT;

          circle_in_round_rect(s, cx[i], eye_cy, eye_w / 2, eye_h[i] / 2,
                               eye_w / 2, px, py, pr, pupil);
        }
    }

  /* Mouth.  mouth_open sets how thick the band is, mouth_curve how much it
   * smiles.
   */

  parabola(s, s->width / 2, pct(s->height, MOUTH_CY_PCT),
           pct(s->width, MOUTH_W_PCT),
           pct(s->height, MOUTH_H_PCT) + (pose->mouth_open * pct(s->height, 6))
           / FACE_UNIT,
           (pose->mouth_curve * pct(s->height, 8)) / FACE_UNIT, trim);

  if (dirty != NULL)
    {
      /* The clear touches everything, so the dirty box is the whole panel.
       * Kept in the interface because a partial redraw is the obvious next
       * optimisation once the frame time is measured.
       */

      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
    }
}
