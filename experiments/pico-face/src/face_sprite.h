/****************************************************************************
 * experiments/pico-face/src/face_sprite.h
 *
 * The palettes and the coarse grid drawing the pixel art presets share.  A
 * preset picks a grid, 48 or 40 cells across, and every shape it draws is
 * blown up to the panel one whole block per cell, so edges land on block
 * boundaries and the result reads as pixel art.  Nothing here touches a
 * framebuffer or NuttX, so it builds and runs on the host under test/.
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

struct face_palette
{
  const char *name;
  uint16_t colour[FACE_PALETTE_SIZE];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Packs eight bit channels into RGB565.  Shared with the renderers so the
 * palettes and the drawn shapes cannot disagree about a colour.
 */

uint16_t face_rgb565(int r, int g, int b);

/* Fills one scale by scale block at grid coordinates px, py, offset to
 * ox, oy on the surface.  Clipped, so a block that falls off the panel is
 * trimmed rather than wrapping.
 */

void face_sprite_block(const struct face_surface *s, int px, int py,
                       int scale, int ox, int oy, uint16_t colour);

/* A filled rectangle in grid coordinates, clipped to a grid by grid area. */

void face_grid_rect(const struct face_surface *s, int grid, int scale,
                    int x0, int y0, int w, int h, uint16_t colour);

/* A filled ellipse in grid coordinates, centred on cx, cy.  Integer only,
 * so the boundary is a squared distance test and the result is blocky on
 * purpose.
 */

void face_grid_ellipse(const struct face_surface *s, int grid, int scale,
                       int cx, int cy, int rx, int ry, uint16_t colour);

/* Fills the whole surface with one colour. */

void face_sprite_clear(const struct face_surface *s, uint16_t colour);

/* The palettes, indexed 0 to FACE_NPALETTES - 1. */

const struct face_palette *face_palette(int index);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_SPRITE_H */
