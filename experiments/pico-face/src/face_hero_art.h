/****************************************************************************
 * experiments/pico-face/src/face_hero_art.h
 *
 * The hero sprites, one pose per expression, in the same order as
 * enum face_state.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_HERO_ART_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_HERO_ART_H

#include "face.h"
#include "face_sprite.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern const struct face_sprite g_hero_poses[FACE_NSTATES];

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_HERO_ART_H */
