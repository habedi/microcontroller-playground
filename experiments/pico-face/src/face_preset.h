/****************************************************************************
 * experiments/pico-face/src/face_preset.h
 *
 * The set of looks the face can wear.  A preset is a name and a render
 * function, so adding one is a new file and a new row in the table.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_PRESET_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_PRESET_H

#include <stdint.h>

#include "face.h"
#include "face_render.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* A renderer gets the pose, and also the state and the clock, because a
 * sprite preset picks its frame from those two rather than from the pose.
 * The vector preset ignores both.
 */

typedef void (*face_render_fn)(const struct face_surface *s,
                               const struct face_pose *pose,
                               enum face_state state,
                               uint32_t now_ms,
                               int palette,
                               struct face_dirty *dirty);

struct face_preset
{
  const char *name;
  face_render_fn render;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* The renderers behind the table.  face_render itself is declared in
 * face_render.h, next to the surface it draws into.
 */

void face_render_pixel(const struct face_surface *s,
                       const struct face_pose *pose,
                       enum face_state state, uint32_t now_ms, int palette,
                       struct face_dirty *dirty);

void face_render_hero(const struct face_surface *s,
                      const struct face_pose *pose,
                      enum face_state state, uint32_t now_ms, int palette,
                      struct face_dirty *dirty);

/* How many presets there are, and one of them by index.  An index outside
 * the table wraps, so the caller can add or subtract one without checking.
 */

int face_preset_count(void);
const struct face_preset *face_preset(int index);

/* Wraps an index into range, including negative ones, so stepping backwards
 * off the front lands on the last preset.
 */

int face_preset_wrap(int index);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_PRESET_H */
