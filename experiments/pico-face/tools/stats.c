/****************************************************************************
 * experiments/pico-face/tools/stats.c
 *
 * Predicts how much of the panel the render loop has to send per frame, for
 * every preset in every state, by running the same tracker the board runs
 * over ten seconds of frames at 30 frames per second.  The bus is the limit
 * on the board, so this number is the one that says whether partial redraw
 * pays, and for which looks.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "face_dirty.h"
#include "face_preset.h"

#define PANEL    240
#define FRAME_MS 33
#define SECONDS  10

static struct face_tracker g_tracker;

int main(void)
{
  uint16_t *pixels = calloc((size_t)PANEL * PANEL, sizeof(uint16_t));
  struct face_surface surf;
  int p;
  int s;

  if (pixels == NULL)
    {
      return 1;
    }

  surf.pixels = pixels;
  surf.width = PANEL;
  surf.height = PANEL;
  surf.stride_px = PANEL;

  printf("%-8s", "preset");
  for (s = 0; s < FACE_NSTATES; s++)
    {
      printf(" %8s", face_state_name((enum face_state)s));
    }

  printf("\n");

  for (p = 0; p < face_preset_count(); p++)
    {
      const struct face_preset *preset = face_preset(p);

      printf("%-8s", preset->name);

      for (s = 0; s < FACE_NSTATES; s++)
        {
          struct face_dirty dirty;
          struct face f;
          unsigned long pushed = 0;
          unsigned long frames = 0;
          uint32_t t;

          /* Settle into the state first, so the blend into it and the first
           * whole frame are not counted.
           */

          face_init(&f, 0);
          face_set_state(&f, (enum face_state)s, 0);
          face_tick(&f, 1000);
          preset->render(&surf, &f.pose, (enum face_state)s, 1000, 0,
                         &dirty);
          face_tracker_reset(&g_tracker);
          face_tracker_scan(&g_tracker, &surf, &dirty);

          for (t = 1000; t < 1000 + SECONDS * 1000; t += FRAME_MS)
            {
              face_tick(&f, t);
              preset->render(&surf, &f.pose, (enum face_state)s, t, 0,
                             &dirty);
              face_tracker_scan(&g_tracker, &surf, &dirty);
              pushed += (unsigned long)dirty.w * (unsigned long)dirty.h;
              frames++;
            }

          printf(" %7lu%%",
                 (pushed * 100ul) / ((unsigned long)PANEL * PANEL * frames));
        }

      printf("\n");
    }

  free(pixels);
  return 0;
}
