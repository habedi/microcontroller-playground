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
  uint16_t frame;
  uint16_t frame_hi;
  uint16_t alert;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bot_colors g_bot_palettes[FACE_NPALETTES] =
{
  /* 0: Cyan */
  {
    RGB(2, 4, 8),
    RGB(0, 70, 110),
    RGB(0, 210, 255),
    RGB(200, 250, 255),
    RGB(18, 26, 36),
    RGB(45, 65, 85),
    RGB(255, 60, 60)
  },
  /* 1: Electric Violet */
  {
    RGB(6, 2, 10),
    RGB(50, 20, 90),
    RGB(170, 90, 255),
    RGB(235, 215, 255),
    RGB(28, 18, 38),
    RGB(70, 48, 90),
    RGB(255, 75, 120)
  },
  /* 2: Amber */
  {
    RGB(8, 6, 2),
    RGB(80, 45, 0),
    RGB(255, 175, 0),
    RGB(255, 245, 190),
    RGB(32, 24, 14),
    RGB(75, 58, 35),
    RGB(255, 80, 40)
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

/* Draws an error X eye for the failed state */

static void draw_bot_x_eye(const struct face_surface *s, int cx, int cy,
                           int w, int h, const struct bot_colors *col)
{
  int half_w = w / 2;
  int half_h = h / 2;
  int dy;

  if (half_w <= 0 || half_h <= 0)
    {
      return;
    }

  for (dy = -half_h; dy <= half_h; dy++)
    {
      int x_diag = (dy * half_w) / (half_h > 0 ? half_h : 1);
      int y = cy + dy;

      span(s, y, cx + x_diag - 3, cx + x_diag + 3, col->alert);
      span(s, y, cx - x_diag - 3, cx - x_diag + 3, col->alert);
      span(s, y, cx + x_diag - 1, cx + x_diag + 1, col->core);
      span(s, y, cx - x_diag - 1, cx - x_diag + 1, col->core);
    }
}

/* Draws a happy curved chevron eye for the done state */

static void draw_bot_arch_eye(const struct face_surface *s, int cx, int cy,
                             int w, int h, const struct bot_colors *col)
{
  int half_w = w / 2;
  int half_h = h / 2;
  int dx;

  if (half_w <= 0 || half_h <= 0)
    {
      return;
    }

  for (dx = -half_w; dx <= half_w; dx++)
    {
      int adx = dx < 0 ? -dx : dx;
      int apex = cy - half_h + (adx * half_h) / (half_w > 0 ? half_w : 1);

      span(s, apex - 3, cx + dx, cx + dx, col->eye);
      span(s, apex - 2, cx + dx, cx + dx, col->core);
      span(s, apex - 1, cx + dx, cx + dx, col->core);
      span(s, apex,     cx + dx, cx + dx, col->core);
      span(s, apex + 1, cx + dx, cx + dx, col->core);
      span(s, apex + 2, cx + dx, cx + dx, col->eye);
    }
}

/* Draws one robot eye capsule with optional top brow tilt and bottom smile cut */

static void draw_bot_eye(const struct face_surface *s, int cx, int cy,
                         int w, int h, int tilt, int smile,
                         const struct bot_colors *col, enum face_state state,
                         uint32_t now_ms)
{
  int half_w = w / 2;
  int half_h = h / 2;
  int radius = half_w > 12 ? 12 : half_w;
  int scan_dy = 0;
  int dy;

  if (half_w <= 0 || half_h <= 0)
    {
      return;
    }

  if (state == FACE_WORKING && half_h > 2)
    {
      scan_dy = -half_h + 2 + (int)((now_ms / 15) % (uint32_t)(2 * half_h - 4));
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

          if (state == FACE_WORKING && (dy == scan_dy || dy == scan_dy + 1))
            {
              span(s, y, x, x, col->core);
            }
          else if (hw > 4 && dy > -half_h + 4 && dy < half_h - 4 &&
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

  if (state == FACE_WAITING)
    {
      eye_h[0] = eye_h_max;
      eye_h[1] = (eye_h_max * 65) / 100;
      gaze_y = -pct(s->height, 4);
    }

  if (state == FACE_DONE && pose->mouth_curve > 0)
    {
      smile = (pose->mouth_curve * 12) / FACE_UNIT;
    }

  for (i = 0; i < s->height; i++)
    {
      span(s, i, 0, s->width - 1, col->bg);
    }

  /* Chassis frame and top antenna beacon on normal sized displays */

  if (s->width >= 100 && s->height >= 100)
    {
      int mid_x = s->width / 2;
      uint16_t beacon_col;
      int y;

      /* Outer rounded bezel */

      span(s, 6, 8, s->width - 9, col->frame);
      span(s, s->height - 7, 8, s->width - 9, col->frame);
      for (y = 8; y < s->height - 8; y++)
        {
          span(s, y, 6, 6, col->frame);
          span(s, y, s->width - 7, s->width - 7, col->frame);
        }

      /* Corner accent rivets */

      span(s, 8, 8, 9, col->frame_hi);
      span(s, 8, s->width - 10, s->width - 9, col->frame_hi);
      span(s, s->height - 9, 8, 9, col->frame_hi);
      span(s, s->height - 9, s->width - 10, s->width - 9, col->frame_hi);

      /* Antenna stem */

      for (y = 10; y <= 18; y++)
        {
          span(s, y, mid_x - 1, mid_x + 1, col->frame_hi);
        }

      /* Antenna beacon light */

      if (state == FACE_FAILED)
        {
          beacon_col = ((now_ms / 150) & 1) ? col->alert : col->glow;
        }
      else if (state == FACE_WORKING)
        {
          beacon_col = ((now_ms / 180) & 1) ? col->core : col->eye;
        }
      else if (state == FACE_EDITING)
        {
          beacon_col = ((now_ms / 120) % 3 == 0) ? col->core : col->glow;
        }
      else if (state == FACE_WAITING)
        {
          int wave_r = (int)((now_ms / 80) % 4);
          beacon_col = col->eye;
          span(s, 6 - wave_r, mid_x - wave_r - 2, mid_x + wave_r + 2, col->glow);
        }
      else if (state == FACE_DONE)
        {
          beacon_col = col->core;
        }
      else
        {
          beacon_col = col->glow;
        }

      span(s, 7, mid_x - 2, mid_x + 2, beacon_col);
      span(s, 8, mid_x - 3, mid_x + 3, beacon_col);
      span(s, 9, mid_x - 2, mid_x + 2, beacon_col);
      span(s, 8, mid_x - 1, mid_x + 1, col->core);

      /* Segmented activity meter below eyes on editing or working */

      if (state == FACE_EDITING || state == FACE_WORKING)
        {
          int my = s->height - 24;
          int active_dot = (int)((now_ms / 150) % 5);
          int k;

          for (k = 0; k < 5; k++)
            {
              int kx = mid_x - 24 + k * 12;
              uint16_t dcol = (k == active_dot) ? col->core : col->frame_hi;

              span(s, my, kx - 2, kx + 2, dcol);
              span(s, my + 1, kx - 2, kx + 2, dcol);
            }
        }
    }

  for (i = 0; i < 2; i++)
    {
      int cy = eye_cy + gaze_y;

      if (state == FACE_FAILED && eye_h[i] > 6)
        {
          draw_bot_x_eye(s, cx[i], cy, eye_w, eye_h[i], col);
        }
      else if (state == FACE_DONE && pose->mouth_curve > 300)
        {
          draw_bot_arch_eye(s, cx[i], cy, eye_w, eye_h[i], col);
        }
      else if (eye_h[i] <= 3)
        {
          span(s, cy, cx[i] - eye_w / 2, cx[i] + eye_w / 2, col->eye);
        }
      else
        {
          draw_bot_eye(s, cx[i], cy, eye_w, eye_h[i], tilt[i], smile, col,
                       state, now_ms);
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
