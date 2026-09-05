/****************************************************************************
 * experiments/pico-face/src/face_preset.c
 *
 * The preset table.  Adding a look means writing its renderer and adding one
 * row here.
 *
 ****************************************************************************/

#include "face_preset.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct face_preset g_presets[] =
{
  { "vector", face_render },
  { "pixel",  face_render_pixel },
  { "crab",   face_render_crab },
};

#define NPRESETS ((int)(sizeof(g_presets) / sizeof(g_presets[0])))

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int face_preset_count(void)
{
  return NPRESETS;
}

int face_preset_wrap(int index)
{
  /* The C remainder keeps the sign of the left operand, so a negative index
   * needs one correction rather than a loop.
   */

  index %= NPRESETS;

  if (index < 0)
    {
      index += NPRESETS;
    }

  return index;
}

const struct face_preset *face_preset(int index)
{
  return &g_presets[face_preset_wrap(index)];
}
