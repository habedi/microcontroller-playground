/****************************************************************************
 * experiments/pico-face/src/face_overlay.c
 *
 * The debug overlay, and the text drawing it needs.  Shows which preset and
 * palette are up, which expression is showing, the measured frame rate, and
 * whether the state file is being ignored.
 *
 ****************************************************************************/

#include <stddef.h>

#include "face_font.h"
#include "face_overlay.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SCALE 2

/* Advance from the start of one character to the next, in panel pixels. */

#define ADVANCE ((FACE_FONT_W + 1) * SCALE)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Index of a character in the font, or the index of a space when the font
 * does not cover it.  Lowercase folds to uppercase, since the font has one
 * case and the preset names are lowercase.
 */

static int glyph_of(char c)
{
  int i;

  if (c >= 'a' && c <= 'z')
    {
      c = (char)(c - 'a' + 'A');
    }

  for (i = 0; g_font_chars[i] != '\0'; i++)
    {
      if (g_font_chars[i] == c)
        {
          return i;
        }
    }

  return 0;
}

/* A filled rectangle in panel coordinates, clipped. */

static void fill_rect(const struct face_surface *s, int x0, int y0,
                      int w, int h, uint16_t colour)
{
  int x;
  int y;

  for (y = y0; y < y0 + h; y++)
    {
      uint16_t *row;

      if (y < 0 || y >= s->height)
        {
          continue;
        }

      row = s->pixels + (size_t)y * (size_t)s->stride_px;

      for (x = x0; x < x0 + w; x++)
        {
          if (x >= 0 && x < s->width)
            {
              row[x] = colour;
            }
        }
    }
}

/* Writes a whole number backwards into the end of a buffer and returns where
 * it starts.  Avoids pulling snprintf in for two small fields.
 */

static char *utoa_end(unsigned int v, char *end)
{
  *--end = '\0';

  do
    {
      *--end = (char)('0' + (v % 10));
      v /= 10;
    }
  while (v != 0);

  return end;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_text(const struct face_surface *s, int x, int y, int scale,
               uint16_t colour, const char *text)
{
  int cx = x;

  for (; *text != '\0'; text++)
    {
      int g = glyph_of(*text);
      int row;

      for (row = 0; row < FACE_FONT_H; row++)
        {
          uint8_t bits = g_font_rows[g][row];
          int col;

          for (col = 0; col < FACE_FONT_W; col++)
            {
              if (bits & (1u << (FACE_FONT_W - 1 - col)))
                {
                  /* face_sprite_block scales the glyph coordinates itself,
                   * so the pen position goes in as the offset.
                   */

                  face_sprite_block(s, col, row, scale, cx, y, colour);
                }
            }
        }

      cx += (FACE_FONT_W + 1) * scale;
    }
}

void face_overlay(const struct face_surface *s,
                  const struct face_palette *pal,
                  const char *preset, const char *palette_name,
                  const char *state, unsigned int fps, int hold, int speed)
{
  char buf[16];
  char *p;
  int line = FACE_FONT_H * SCALE + 3;
  int rows = hold ? 5 : 4;
  int y = 4;

  /* A band behind the text, so it stays readable over pale art. */

  fill_rect(s, 0, 0, s->width, rows * line + 6, pal->colour[1]);

  face_text(s, 4, y, SCALE, pal->colour[14], preset);
  face_text(s, 4 + 8 * ADVANCE, y, SCALE, pal->colour[10], palette_name);
  y += line;

  face_text(s, 4, y, SCALE, pal->colour[14], state);
  y += line;

  p = utoa_end(fps, buf + sizeof(buf));
  face_text(s, 4, y, SCALE, pal->colour[14], "FPS");
  face_text(s, 4 + 4 * ADVANCE, y, SCALE, pal->colour[14], p);
  y += line;

  p = utoa_end((unsigned int)speed, buf + sizeof(buf));
  face_text(s, 4, y, SCALE, pal->colour[14], "SPD");
  face_text(s, 4 + 4 * ADVANCE, y, SCALE, pal->colour[14], p);
  y += line;

  if (hold)
    {
      face_text(s, 4, y, SCALE, pal->colour[12], "HOLD");
    }
}
