/****************************************************************************
 * experiments/pico-face/src/face_overlay.h
 *
 * The debug overlay the Y button toggles, drawn over whichever preset is up.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_OVERLAY_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_OVERLAY_H

#include <stdint.h>

#include "face_render.h"
#include "face_sprite.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Draws text with the overlay font.  The scale is a whole number, so the
 * glyphs stay on pixel boundaries like everything else here.
 */

void face_text(const struct face_surface *s, int x, int y, int scale,
               uint16_t colour, const char *text);

/* The overlay itself, in the top left corner.  bus is the share of a full
 * frame that went over the SPI bus per frame, as a percentage.
 */

void face_overlay(const struct face_surface *s,
                  const struct face_palette *pal,
                  const char *preset, const char *palette_name,
                  const char *state, unsigned int fps, unsigned int bus,
                  int hold, int speed);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_OVERLAY_H */
