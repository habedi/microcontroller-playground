/****************************************************************************
 * experiments/pico-face/src/face_hero.c
 *
 * The hero preset.  One 32 by 32 pose per expression, scaled up to the
 * panel.  The pose comes from the state rather than from struct face_pose,
 * because at this size an eyelid is one pixel and a blink would not read.
 *
 * The idle motion is a one pixel bob applied to the whole sprite rather than
 * a second frame, which animates the character for no extra art.
 *
 ****************************************************************************/

#include <stddef.h>

#include "face_hero_art.h"
#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPR 32

/* Half a period of the bob, in milliseconds. */

#define BOB_MS 620

/* Palette roles used for the ground and the backdrop. */

#define C_BG      0
#define C_OUTLINE 1
#define C_CLOTH_D 9

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* A flat shadow under the feet, which is what stops the sprite looking like
 * it is floating on the backdrop.
 */

static void ground_shadow(const struct face_surface *s, int scale,
                          int ox, int oy, uint16_t colour)
{
  int cx = SPR / 2;
  int cy = SPR - 2;
  int x;

  for (x = cx - 8; x <= cx + 8; x++)
    {
      int dx = x - cx;
      int h = dx > -4 && dx < 4 ? 2 : 1;
      int y;

      for (y = cy; y < cy + h; y++)
        {
          face_sprite_block(s, x, y, scale, ox, oy, colour);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_render_hero(const struct face_surface *s,
                      const struct face_pose *pose,
                      enum face_state state,
                      uint32_t now_ms,
                      int palette,
                      struct face_dirty *dirty)
{
  const struct face_palette *pal = face_palette(palette);
  int scale = s->width / SPR;
  int ox;
  int oy;
  int bob;

  if (scale < 1)
    {
      scale = 1;
    }

  /* Centre the scaled sprite, since 32 does not divide the panel evenly. */

  ox = (s->width - SPR * scale) / 2;
  oy = (s->height - SPR * scale) / 2;

  /* The bob rests while the hero is down, because a knocked over character
   * that keeps breathing at the same rate reads as a mistake.
   */

  bob = 0;
  if (state != FACE_FAILED)
    {
      bob = ((now_ms / BOB_MS) & 1) ? scale : 0;
    }

  face_sprite_clear(s, pal->colour[C_BG]);

  ground_shadow(s, scale, ox, oy, pal->colour[C_CLOTH_D]);

  if (state < 0 || state >= FACE_NSTATES)
    {
      state = FACE_IDLE;
    }

  face_sprite_blit(s, &g_hero_poses[state], pal, ox, oy - bob, scale);

  /* The pose is unused here by design.  It stays in the signature because
   * the pixel and vector presets need it.
   */

  (void)pose;

  if (dirty != NULL)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
    }
}
