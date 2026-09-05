/****************************************************************************
 * experiments/pico-face/src/face_dirty.h
 *
 * Finds what changed between one frame and the next, so the render loop can
 * push only that part of the panel over SPI.  The presets keep redrawing the
 * whole surface, which is cheap; what is not cheap is sending it, and this
 * is what decides how much to send.
 *
 * Nothing here touches a framebuffer or NuttX, so it builds and runs on the
 * host under test/.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_DIRTY_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_DIRTY_H

#include <stdint.h>

#include "face_render.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The largest surface the tracker follows.  A bigger one is pushed whole. */

#define FACE_TRACK_MAX 320

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* One hash per row and per pair of columns of the last frame that was
 * pushed.  A changed pixel changes the hash of its row and of its column
 * pair, and the changed rows and columns bound the changed area.  That box
 * can be looser than the true change, two eyes blinking become one wide box
 * and its sides fall on even pixels, but it is never tighter, and it needs
 * 4 KB rather than a second copy of the frame.
 */

struct face_tracker
{
  uint32_t rows[FACE_TRACK_MAX];
  uint32_t cols[FACE_TRACK_MAX];
  uint32_t scratch[FACE_TRACK_MAX];  /* the new column hashes, while the
                                      * old ones are still being compared.
                                      * Here rather than on the stack, since
                                      * the render task has 4 KB of it. */
  int primed;   /* 0 until the first frame has been seen */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Forgets the last frame, so the next call reports the whole surface. */

void face_tracker_reset(struct face_tracker *t);

/* Compares the surface with the last frame this tracker saw, writes the
 * bounding box of the difference to dirty, and remembers the surface.  A
 * frame with no change gives a box of zero width and height.  The first
 * frame after a reset, a surface larger than FACE_TRACK_MAX, and one with
 * an odd width or stride or unaligned pixels give the whole surface.
 */

void face_tracker_scan(struct face_tracker *t, const struct face_surface *s,
                       struct face_dirty *dirty);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_DIRTY_H */
