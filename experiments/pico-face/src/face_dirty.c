/****************************************************************************
 * experiments/pico-face/src/face_dirty.c
 *
 * Change detection by row and column hashes.  One pass over the frame
 * updates both sets.  Pixels are read two at a time as one 32 bit word, so
 * the column hashes are per pair of columns and the box's left and right
 * edges land on even pixels.  Halving the loads and stores is what makes
 * this cheap enough to run every frame on the M33.
 *
 ****************************************************************************/

#include <string.h>

#include "face_dirty.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* FNV-1a, folded over 16 bit pixels.  Any change to a row or column is very
 * unlikely to leave its hash unchanged, and a miss costs one late pixel
 * rather than a crash, so a 32 bit hash is enough.
 */

#define FNV_OFFSET 2166136261u
#define FNV_PRIME  16777619u

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_tracker_reset(struct face_tracker *t)
{
  t->primed = 0;
}

void face_tracker_scan(struct face_tracker *t, const struct face_surface *s,
                       struct face_dirty *dirty)
{
  uint32_t *cols = t->scratch;
  int pairs = (s->width + 1) / 2;
  int x0 = pairs;
  int x1 = -1;
  int y0 = s->height;
  int y1 = -1;
  int x;
  int y;

  /* An odd width or an unaligned row would need a scalar tail.  No panel
   * this runs on has either, so such a surface is simply pushed whole.
   */

  if (s->width > FACE_TRACK_MAX || s->height > FACE_TRACK_MAX ||
      (s->width & 1) != 0 || (s->stride_px & 1) != 0 ||
      ((uintptr_t)s->pixels & 3) != 0)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
      return;
    }

  for (x = 0; x < pairs; x++)
    {
      cols[x] = FNV_OFFSET;
    }

  for (y = 0; y < s->height; y++)
    {
      const uint32_t *row = (const uint32_t *)
                            (s->pixels + (size_t)y * (size_t)s->stride_px);
      uint32_t h = FNV_OFFSET;

      for (x = 0; x < pairs; x++)
        {
          uint32_t px = row[x];

          h = (h ^ px) * FNV_PRIME;
          cols[x] = (cols[x] ^ px) * FNV_PRIME;
        }

      if (!t->primed || h != t->rows[y])
        {
          if (y < y0)
            {
              y0 = y;
            }

          y1 = y;
        }

      t->rows[y] = h;
    }

  for (x = 0; x < pairs; x++)
    {
      if (!t->primed || cols[x] != t->cols[x])
        {
          if (x < x0)
            {
              x0 = x;
            }

          x1 = x;
        }

      t->cols[x] = cols[x];
    }

  t->primed = 1;

  if (y1 < 0 || x1 < 0)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = 0;
      dirty->h = 0;
      return;
    }

  dirty->x = 2 * x0;
  dirty->y = y0;
  dirty->w = 2 * (x1 - x0 + 1);
  dirty->h = y1 - y0 + 1;
}
