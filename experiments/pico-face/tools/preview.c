/****************************************************************************
 * experiments/pico-face/tools/preview.c
 *
 * Renders every preset against every expression into one contact sheet on
 * the host, so the art can be judged without flashing the board.  One row per
 * preset, one column per expression.  Writes a binary PPM to standard output.
 *
 * Takes an optional time, palette, preset name, and time step.  A time step
 * makes a motion sheet with one row per state and eight moments per row.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PANEL 240
#define GAP   8

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Copies one rendered panel into the sheet at a tile position. */

static void blit(uint16_t *sheet, int sheet_w, const uint16_t *panel,
                 int col, int row)
{
  int x0 = GAP + col * (PANEL + GAP);
  int y0 = GAP + row * (PANEL + GAP);
  int y;

  for (y = 0; y < PANEL; y++)
    {
      memcpy(sheet + (size_t)(y0 + y) * (size_t)sheet_w + (size_t)x0,
             panel + (size_t)y * PANEL,
             PANEL * sizeof(uint16_t));
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char **argv)
{
  int cols = FACE_NSTATES;
  int rows = face_preset_count();
  int sheet_w;
  int sheet_h;
  uint16_t *sheet;
  uint16_t *panel;
  uint32_t when_ms = 500;
  int palette = 0;
  int selected = -1;
  uint32_t step_ms = 0;
  int p;
  int s;
  int y;
  int x;

  if (argc > 1)
    {
      when_ms = (uint32_t)strtoul(argv[1], NULL, 10);
    }

  if (argc > 2)
    {
      palette = (int)strtol(argv[2], NULL, 10);
    }

  if (argc > 3)
    {
      selected = face_preset_find(argv[3]);
      if (selected < 0)
        {
          fprintf(stderr, "unknown preset: %s\n", argv[3]);
          return 1;
        }

      rows = 1;
    }

  if (argc > 4)
    {
      step_ms = (uint32_t)strtoul(argv[4], NULL, 10);
      if (step_ms > 0)
        {
          cols = 8;
          rows = FACE_NSTATES;
        }
    }

  sheet_w = GAP + cols * (PANEL + GAP);
  sheet_h = GAP + rows * (PANEL + GAP);

  sheet = calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(uint16_t));
  panel = calloc((size_t)PANEL * PANEL, sizeof(uint16_t));

  if (sheet == NULL || panel == NULL)
    {
      free(sheet);
      free(panel);
      return 1;
    }

  fprintf(stderr, "t=%lu ms, palette %d (%s)\n", (unsigned long)when_ms,
          palette, face_palette(palette)->name);

  for (p = 0; p < rows; p++)
    {
      const struct face_preset *preset = face_preset(selected >= 0 ? selected : p);

      fprintf(stderr, "  row %d: %s\n", p,
              step_ms ? face_state_name((enum face_state)p) : preset->name);

      for (s = 0; s < cols; s++)
        {
          struct face_surface surf;
          struct face_dirty dirty;
          struct face f;
          enum face_state state = (enum face_state)(step_ms ? p : s);
          uint32_t sample_ms = when_ms + (uint32_t)s * step_ms;

          surf.pixels = panel;
          surf.width = PANEL;
          surf.height = PANEL;
          surf.stride_px = PANEL;

          face_init(&f, 0);
          face_set_state(&f, state, 0);
          face_tick(&f, sample_ms);

          preset->render(&surf, &f.pose, state, sample_ms,
                         palette, &dirty);

          blit(sheet, sheet_w, panel, s, p);
        }
    }

  printf("P6\n%d %d\n255\n", sheet_w, sheet_h);

  for (y = 0; y < sheet_h; y++)
    {
      for (x = 0; x < sheet_w; x++)
        {
          uint16_t c = sheet[(size_t)y * (size_t)sheet_w + (size_t)x];
          unsigned char rgb[3];

          /* Expand RGB565 back to eight bits per channel, replicating the
           * high bits so full scale stays full scale.
           */

          rgb[0] = (unsigned char)(((c >> 11) & 0x1f) * 255 / 31);
          rgb[1] = (unsigned char)(((c >> 5) & 0x3f) * 255 / 63);
          rgb[2] = (unsigned char)((c & 0x1f) * 255 / 31);

          fwrite(rgb, 1, 3, stdout);
        }
    }

  free(sheet);
  free(panel);
  return 0;
}
