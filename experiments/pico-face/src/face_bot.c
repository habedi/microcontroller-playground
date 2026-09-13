/****************************************************************************
 * experiments/pico-face/src/face_bot.c
 *
 * Minimalist robot eye preset: two pill-shaped eyes on a black ground.
 *
 ****************************************************************************/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define EYE_W_PCT    23
#define EYE_H_PCT    27
#define EYE_CY_PCT   48
#define EYE_CX_L_PCT 33
#define EYE_CX_R_PCT 67

#define RGB(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | \
                                 (((g) & 0xfc) << 3) | \
                                 ((b) >> 3)))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bot_colors
{
  uint16_t bg;
  uint16_t glow;
  uint16_t eye;
  uint16_t core;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bot_colors g_bot_palettes[FACE_NPALETTES] =
{
  /* 0: Cyan */
  {
    RGB(0, 0, 0),
    RGB(0, 70, 110),
    RGB(0, 210, 255),
    RGB(200, 250, 255)
  },
  /* 1: Electric Violet */
  {
    RGB(0, 0, 0),
    RGB(50, 20, 90),
    RGB(170, 90, 255),
    RGB(235, 215, 255)
  },
  /* 2: Amber */
  {
    RGB(0, 0, 0),
    RGB(80, 45, 0),
    RGB(255, 175, 0),
    RGB(255, 245, 190)
  }
};

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

  if (x0 > x1)
    {
      return;
    }

  row = s->pixels + (size_t)y * (size_t)s->stride_px;

  for (x = x0; x <= x1; x++)
    {
      row[x] = colour;
    }
}

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

  over = r - (half_h - adist);

  if (over <= 0)
    {
      return half_w;
    }

  return half_w - (r - (int)isqrt32((uint32_t)(r * r - over * over)));
}

/* Draws one robot eye capsule with optional top brow tilt and bottom smile cut */

static void draw_bot_eye(const struct face_surface *s, int cx, int cy,
                         int w, int h, int tilt, int smile,
                         const struct bot_colors *col)
{
  int half_w = w / 2;
  int half_h = h / 2;
  int radius = half_w > 12 ? 12 : half_w;
  int dy;

  if (half_w <= 0 || half_h <= 0)
    {
      return;
    }

  for (dy = -half_h; dy <= half_h; dy++)
    {
      int hw = round_rect_half(half_w, half_h, radius, dy);
      int y = cy + dy;
      int x0;
      int x1;
      int x;

      if (hw < 0)
        {
          continue;
        }

      x0 = cx - hw;
      x1 = cx + hw;

      for (x = x0; x <= x1; x++)
        {
          int x_norm = ((x - cx) * tilt) / half_w;
          int smile_cut = 0;

          if (smile > 0)
            {
              int dx_from_c = x - cx;
              smile_cut = ((half_w * half_w - dx_from_c * dx_from_c) * smile)
                          / (half_w * half_w + 1);
            }

          if (dy < -half_h / 2 + x_norm)
            {
              continue;
            }

          if (smile > 0 && dy > half_h - smile_cut)
            {
              continue;
            }

          if (hw > 4 && dy > -half_h + 4 && dy < half_h - 4 &&
              x > cx - hw + 4 && x < cx + hw - 4)
            {
              span(s, y, x, x, col->core);
            }
          else
            {
              span(s, y, x, x, col->eye);
            }
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_render_bot(const struct face_surface *s,
                     const struct face_pose *pose,
                     enum face_state state,
                     uint32_t now_ms,
                     int palette,
                     struct face_dirty *dirty)
{
  const struct bot_colors *col;
  int eye_w = pct(s->width, EYE_W_PCT);
  int eye_h_max = pct(s->height, EYE_H_PCT);
  int eye_cy = pct(s->height, EYE_CY_PCT);
  int pal_idx = palette % FACE_NPALETTES;
  int gaze_x;
  int gaze_y;
  int eye_h[2];
  int cx[2];
  int i;
  int tilt[2];
  int smile = 0;

  (void)now_ms;

  if (pal_idx < 0)
    {
      pal_idx += FACE_NPALETTES;
    }

  col = &g_bot_palettes[pal_idx];

  gaze_x = (pose->pupil_x * pct(s->width, 4)) / FACE_UNIT;
  gaze_y = (pose->pupil_y * pct(s->height, 5)) / FACE_UNIT;

  cx[0] = pct(s->width, EYE_CX_L_PCT) + gaze_x;
  cx[1] = pct(s->width, EYE_CX_R_PCT) + gaze_x;

  eye_h[0] = (eye_h_max * pose->eye_open_l) / FACE_UNIT;
  eye_h[1] = (eye_h_max * pose->eye_open_r) / FACE_UNIT;

  tilt[0] = -(pose->brow * 8) / FACE_UNIT;
  tilt[1] = (pose->brow * 8) / FACE_UNIT;

  if (state == FACE_DONE && pose->mouth_curve > 0)
    {
      smile = (pose->mouth_curve * 12) / FACE_UNIT;
    }

  for (i = 0; i < s->height; i++)
    {
      span(s, i, 0, s->width - 1, col->bg);
    }

  for (i = 0; i < 2; i++)
    {
      int cy = eye_cy + gaze_y;

      if (eye_h[i] <= 3)
        {
          span(s, cy, cx[i] - eye_w / 2, cx[i] + eye_w / 2, col->eye);
        }
      else
        {
          draw_bot_eye(s, cx[i], cy, eye_w, eye_h[i], tilt[i], smile, col);
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
