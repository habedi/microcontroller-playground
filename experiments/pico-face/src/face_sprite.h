/****************************************************************************
 * experiments/pico-face/src/face_sprite.h
 *
 * Indexed sprites written as ASCII art, plus the palettes they draw with and
 * the scaled blit that puts them on a surface.  Nothing here touches a
 * framebuffer or NuttX, so it builds and runs on the host under test/.
 *
 * A row is one string, one character per pixel.  A dot is transparent and
 * the digits 0 to 9 and the letters a to f select a palette entry, so the
 * art stays readable and editable in a text editor.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_SPRITE_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_SPRITE_H

#include <stdint.h>

#include "face_render.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FACE_PALETTE_SIZE 16

/* How many palettes the B button cycles through. */

#define FACE_NPALETTES 3

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct face_sprite
{
  int w;                     /* every row must be exactly this long */
  int h;                     /* number of rows */
  const char *const *rows;
};

struct face_palette
{
  const char *name;
  uint16_t colour[FACE_PALETTE_SIZE];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Palette index for one art character, or -1 when the pixel is transparent.
 * Any character that is not a dot and not a hex digit is also transparent,
 * which keeps a typo in the art from drawing a wrong colour silently.
 */

int face_sprite_index(char c);

/* Packs eight bit channels into RGB565.  Shared with the renderers so the
 * palettes and the drawn shapes cannot disagree about a colour.
 */

uint16_t face_rgb565(int r, int g, int b);

/* Fills one scale by scale block at sprite coordinates px, py, offset to
 * ox, oy on the surface.  Clipped, so a block that falls off the panel is
 * trimmed rather than wrapping.
 */

void face_sprite_block(const struct face_surface *s, int px, int py,
                       int scale, int ox, int oy, uint16_t colour);

/* Draws a sprite at ox, oy, scaled by an integer factor. */

void face_sprite_blit(const struct face_surface *s,
                      const struct face_sprite *spr,
                      const struct face_palette *pal,
                      int ox, int oy, int scale);

/* Fills the whole surface with one colour. */

void face_sprite_clear(const struct face_surface *s, uint16_t colour);

/* The palettes, indexed 0 to FACE_NPALETTES - 1. */

const struct face_palette *face_palette(int index);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_SPRITE_H */
