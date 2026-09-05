/****************************************************************************
 * experiments/pico-face/src/face_sprite.c
 *
 * Palettes and the grid drawing shared by the pixel art presets.
 *
 ****************************************************************************/

#include <stddef.h>

#include "face_sprite.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Written as eight bit triples and packed here, because a wrong digit in a
 * raw RGB565 constant is invisible in review.
 */

#define RGB(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | \
                                 (((g) & 0xfc) << 3) | \
                                 ((b) >> 3)))

/* Three palettes with the same roles in every slot, so one sprite draws
 * correctly under all of them and the B button can swap between them without
 * the art knowing.  The roles are, in order: background, outline, three skin
 * tones, two hair tones, three cloth tones, two metal tones, two accent
 * tones, a highlight, and the eye colour.
 */

static const struct face_palette g_palettes[FACE_NPALETTES] =
{
  {
    "day",
    {
      RGB(10, 9, 14),      RGB(24, 20, 28),     RGB(255, 214, 170),
      RGB(232, 180, 136),  RGB(188, 132, 96),   RGB(250, 200, 90),
      RGB(170, 120, 40),   RGB(70, 140, 200),   RGB(44, 96, 150),
      RGB(26, 60, 100),    RGB(210, 214, 220),  RGB(130, 138, 150),
      RGB(220, 70, 70),    RGB(150, 40, 45),    RGB(255, 255, 255),
      RGB(60, 120, 230)
    }
  },
  {
    "twilight",
    {
      RGB(8, 8, 18),       RGB(20, 18, 34),     RGB(225, 205, 225),
      RGB(190, 168, 200),  RGB(140, 120, 160),  RGB(200, 180, 255),
      RGB(120, 100, 180),  RGB(90, 110, 190),   RGB(58, 72, 140),
      RGB(34, 42, 92),     RGB(190, 200, 225),  RGB(112, 124, 160),
      RGB(170, 90, 220),   RGB(110, 50, 150),   RGB(255, 255, 255),
      RGB(150, 220, 255)
    }
  },
  {
    "ember",
    {
      RGB(16, 8, 8),       RGB(32, 16, 14),     RGB(255, 222, 180),
      RGB(240, 180, 130),  RGB(200, 124, 80),   RGB(255, 180, 60),
      RGB(180, 90, 30),    RGB(200, 90, 60),    RGB(150, 58, 40),
      RGB(100, 34, 26),    RGB(230, 220, 200),  RGB(150, 130, 110),
      RGB(255, 200, 60),   RGB(200, 130, 30),   RGB(255, 255, 255),
      RGB(255, 230, 120)
    }
  }
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint16_t face_rgb565(int r, int g, int b)
{
  if (r < 0)
    {
      r = 0;
    }
  else if (r > 255)
    {
      r = 255;
    }

  if (g < 0)
    {
      g = 0;
    }
  else if (g > 255)
    {
      g = 255;
    }

  if (b < 0)
    {
      b = 0;
    }
  else if (b > 255)
    {
      b = 255;
    }

  return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

void face_sprite_block(const struct face_surface *s, int px, int py,
                       int scale, int ox, int oy, uint16_t colour)
{
  int x0 = ox + px * scale;
  int y0 = oy + py * scale;
  int x;
  int y;

  for (y = y0; y < y0 + scale; y++)
    {
      uint16_t *row;

      if (y < 0 || y >= s->height)
        {
          continue;
        }

      row = s->pixels + (size_t)y * (size_t)s->stride_px;

      for (x = x0; x < x0 + scale; x++)
        {
          if (x >= 0 && x < s->width)
            {
              row[x] = colour;
            }
        }
    }
}

void face_grid_rect(const struct face_surface *s, int grid, int scale,
                    int x0, int y0, int w, int h, uint16_t colour)
{
  int x;
  int y;

  for (y = y0; y < y0 + h; y++)
    {
      if (y < 0 || y >= grid)
        {
          continue;
        }

      for (x = x0; x < x0 + w; x++)
        {
          if (x >= 0 && x < grid)
            {
              face_sprite_block(s, x, y, scale, 0, 0, colour);
            }
        }
    }
}

void face_grid_ellipse(const struct face_surface *s, int grid, int scale,
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

      if (y < 0 || y >= grid)
        {
          continue;
        }

      for (x = cx - rx; x <= cx + rx; x++)
        {
          int dx = x - cx;

          if (x < 0 || x >= grid)
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

void face_sprite_clear(const struct face_surface *s, uint16_t colour)
{
  int x;
  int y;

  for (y = 0; y < s->height; y++)
    {
      uint16_t *row = s->pixels + (size_t)y * (size_t)s->stride_px;

      for (x = 0; x < s->width; x++)
        {
          row[x] = colour;
        }
    }
}

const struct face_palette *face_palette(int index)
{
  if (index < 0 || index >= FACE_NPALETTES)
    {
      index = 0;
    }

  return &g_palettes[index];
}
