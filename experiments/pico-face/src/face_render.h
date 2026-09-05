/****************************************************************************
 * experiments/pico-face/src/face_render.h
 *
 * Draws a pose into a 16 bit RGB565 surface.  The surface is supplied by the
 * caller, so this file knows nothing about framebuffers or NuttX and can be
 * exercised on the host.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_RENDER_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_RENDER_H

#include <stdint.h>

#include "face.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct face_surface
{
  uint16_t *pixels;  /* RGB565, one row every stride_px entries */
  int width;
  int height;
  int stride_px;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Draws the whole surface.  Returns the bounding box that changed, so a
 * caller driving a panel over SPI can push only that part.
 */

struct face_dirty
{
  int x;
  int y;
  int w;
  int h;
};

/* The vector preset.  Takes the state, the clock, and a palette index for
 * signature compatibility with the pixel presets, and uses none of them.
 */

void face_render(const struct face_surface *s, const struct face_pose *pose,
                 enum face_state state, uint32_t now_ms, int palette,
                 struct face_dirty *dirty);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_RENDER_H */
