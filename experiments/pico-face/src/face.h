/****************************************************************************
 * experiments/pico-face/src/face.h
 *
 * Expression state machine for the 240x240 panel on the Raspberry Pi Pico 2
 * WH.  This header and face.c hold no board code and no floating point, so
 * they build and run on the host under test/.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_H

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Every pose field is fixed point on a 0 to FACE_UNIT scale, or -FACE_UNIT
 * to FACE_UNIT where the field is signed.  There is no float anywhere.
 */

#define FACE_UNIT 1000

/* How long a state change takes to blend, in milliseconds. */

#define FACE_BLEND_MS 220

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum face_state
{
  FACE_IDLE = 0,    /* Nothing happening.  Slow blink, pupils drift. */
  FACE_WORKING,     /* A tool is running.  Eyes narrow, pupils sweep. */
  FACE_EDITING,     /* Files are changing.  Looking down. */
  FACE_WAITING,     /* Waiting on the user.  Wide eyes, raised brows. */
  FACE_FAILED,      /* The last tool reported an error.  Squint. */
  FACE_DONE,        /* The turn finished.  Eyes close, mouth curves. */
  FACE_NSTATES
};

/* One frame of the face.  The renderer needs nothing else. */

struct face_pose
{
  int16_t eye_open_l;   /* 0 shut, FACE_UNIT fully open */
  int16_t eye_open_r;
  int16_t pupil_x;      /* -FACE_UNIT left, +FACE_UNIT right */
  int16_t pupil_y;      /* -FACE_UNIT up, +FACE_UNIT down */
  int16_t brow;         /* -FACE_UNIT lowered, +FACE_UNIT raised */
  int16_t mouth_curve;  /* -FACE_UNIT frown, +FACE_UNIT smile */
  int16_t mouth_open;   /* 0 shut, FACE_UNIT fully open */
  int16_t glow;         /* 0 dim, FACE_UNIT bright.  Drives the pulse. */
};

struct face
{
  enum face_state state;
  struct face_pose pose;      /* what the renderer draws */
  struct face_pose blend_from;/* pose captured at the last state change */
  uint32_t state_ms;          /* time of the last state change */
  uint32_t now_ms;            /* time of the last tick */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void face_init(struct face *f, uint32_t now_ms);
void face_set_state(struct face *f, enum face_state state, uint32_t now_ms);
void face_tick(struct face *f, uint32_t now_ms);

/* Look up a state by the word the hook sends.  Returns 0 on success and -1
 * when the name is not one of the six.
 */

int face_state_from_name(const char *name, enum face_state *state);
const char *face_state_name(enum face_state state);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_H */
