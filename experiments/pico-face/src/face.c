/****************************************************************************
 * experiments/pico-face/src/face.c
 *
 * Expression state machine for the 240x240 panel.  No board code, no
 * floating point, and every pose is a pure function of the state, the time
 * the state was entered, and the current time.  That is what lets the render
 * loop drop frames without the animation drifting, and what lets the host
 * tests reproduce a frame exactly.
 *
 ****************************************************************************/

#include <string.h>

#include "face.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* One full turn of the phase used by sin_unit(). */

#define PHASE_FULL    1024
#define PHASE_QUARTER (PHASE_FULL / 4)

/* Idle blinks on this period, and each blink lasts this long. */

#define BLINK_PERIOD_MS 3200
#define BLINK_MS        150

/* Shifts the blink phase so the face does not blink the instant it starts. */

#define BLINK_OFFSET_MS 1000

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* One quarter of a sine, from 0 to 90 degrees in 16 steps, scaled to
 * FACE_UNIT.  Interpolating between entries is smooth enough at 240 pixels
 * and costs no multiply-heavy maths.
 */

static const int16_t g_quarter_sin[17] =
{
  0, 98, 195, 290, 383, 471, 556, 634, 707,
  773, 832, 882, 924, 956, 980, 995, 1000
};

static const char *const g_state_names[FACE_NSTATES] =
{
  "idle", "working", "editing", "waiting", "failed", "done"
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
  if (v < lo)
    {
      return lo;
    }

  if (v > hi)
    {
      return hi;
    }

  return v;
}

/* Rising quarter of the sine, for r from 0 to PHASE_QUARTER inclusive. */

static int32_t quarter(uint32_t r)
{
  uint32_t idx;
  uint32_t frac;
  int32_t a;
  int32_t b;

  if (r >= PHASE_QUARTER)
    {
      return FACE_UNIT;
    }

  idx  = r / 16;
  frac = r % 16;
  a    = g_quarter_sin[idx];
  b    = g_quarter_sin[idx + 1];

  return a + ((b - a) * (int32_t)frac) / 16;
}

/* Sine of a phase in 0 to PHASE_FULL, scaled to plus or minus FACE_UNIT. */

static int32_t sin_unit(uint32_t phase)
{
  uint32_t p = phase % PHASE_FULL;
  uint32_t r = p % PHASE_QUARTER;

  switch (p / PHASE_QUARTER)
    {
      case 0:
        return quarter(r);

      case 1:
        return quarter(PHASE_QUARTER - r);

      case 2:
        return -quarter(r);

      default:
        return -quarter(PHASE_QUARTER - r);
    }
}

/* Phase of a cycle of the given period at the given time.  The modulo comes
 * first so a long uptime cannot overflow the multiply.
 */

static uint32_t phase_of(uint32_t now_ms, uint32_t period_ms)
{
  return ((now_ms % period_ms) * PHASE_FULL) / period_ms;
}

/* A cycle scaled to an amplitude, which is how every drifting or pulsing
 * field in target_pose() is written.
 */

static int32_t wave(uint32_t now_ms, uint32_t period_ms, int32_t amplitude)
{
  return (sin_unit(phase_of(now_ms, period_ms)) * amplitude) / FACE_UNIT;
}

/* Eyelid position for a blinking state.  Fully open except for a short dip
 * to shut and back, once per BLINK_PERIOD_MS.
 */

static int32_t blink(uint32_t now_ms, int32_t open)
{
  uint32_t p = (now_ms + BLINK_OFFSET_MS) % BLINK_PERIOD_MS;
  int32_t half = BLINK_MS / 2;

  if (p >= BLINK_MS)
    {
      return open;
    }

  if ((int32_t)p < half)
    {
      return open - (open * (int32_t)p) / half;
    }

  return (open * ((int32_t)p - half)) / half;
}

/* The pose a state wants at a given time, before any blending.  This is the
 * whole of the face's personality, and the only place to edit it.
 */

static void target_pose(enum face_state state, uint32_t now_ms,
                        struct face_pose *p)
{
  memset(p, 0, sizeof(*p));

  switch (state)
    {
      case FACE_WORKING:

        /* Eyes narrowed, pupils sweeping as if reading. */

        p->eye_open_l = 620;
        p->eye_open_r = 620;
        p->pupil_x    = (int16_t)wave(now_ms, 1600, 620);
        p->brow       = -120;
        p->glow       = (int16_t)(600 + wave(now_ms, 900, 250));
        break;

      case FACE_EDITING:

        /* Looking down at the work. */

        p->eye_open_l = 780;
        p->eye_open_r = 780;
        p->pupil_x    = (int16_t)wave(now_ms, 2400, 250);
        p->pupil_y    = 820;
        p->brow       = -80;
        p->glow       = 550;
        break;

      case FACE_WAITING:

        /* Wide awake, brows up, breathing glow.  Meant to catch your eye
         * from across the room.
         */

        p->eye_open_l = FACE_UNIT;
        p->eye_open_r = FACE_UNIT;
        p->pupil_y    = -120;
        p->brow       = 600;
        p->mouth_open = 220;
        p->glow       = (int16_t)(600 + wave(now_ms, 1200, 400));
        break;

      case FACE_FAILED:
        p->eye_open_l  = 280;
        p->eye_open_r  = 280;
        p->brow        = -520;
        p->mouth_curve = -420;
        p->glow        = 220;
        break;

      case FACE_DONE:
        p->eye_open_l  = 130;
        p->eye_open_r  = 130;
        p->brow        = 120;
        p->mouth_curve = 620;
        p->mouth_open  = 120;
        p->glow        = (int16_t)(400 + wave(now_ms, 3000, 120));
        break;

      case FACE_IDLE:
      default:

        /* Awake, blinking, eyes wandering slightly. */

        p->eye_open_l  = (int16_t)blink(now_ms, FACE_UNIT);
        p->eye_open_r  = p->eye_open_l;
        p->pupil_x     = (int16_t)wave(now_ms, 7000, 200);
        p->pupil_y     = (int16_t)wave(now_ms, 9000, 120);
        p->mouth_curve = 150;
        p->glow        = (int16_t)(250 + wave(now_ms, 5000, 80));
        break;
    }

  /* Clamp here rather than at every assignment above, so a new expression
   * cannot put the renderer out of range by accident.
   */

  p->eye_open_l  = (int16_t)clamp(p->eye_open_l, 0, FACE_UNIT);
  p->eye_open_r  = (int16_t)clamp(p->eye_open_r, 0, FACE_UNIT);
  p->pupil_x     = (int16_t)clamp(p->pupil_x, -FACE_UNIT, FACE_UNIT);
  p->pupil_y     = (int16_t)clamp(p->pupil_y, -FACE_UNIT, FACE_UNIT);
  p->brow        = (int16_t)clamp(p->brow, -FACE_UNIT, FACE_UNIT);
  p->mouth_curve = (int16_t)clamp(p->mouth_curve, -FACE_UNIT, FACE_UNIT);
  p->mouth_open  = (int16_t)clamp(p->mouth_open, 0, FACE_UNIT);
  p->glow        = (int16_t)clamp(p->glow, 0, FACE_UNIT);
}

static int16_t mix(int16_t from, int16_t to, int32_t frac)
{
  return (int16_t)(from + (((int32_t)to - (int32_t)from) * frac) / FACE_UNIT);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_init(struct face *f, uint32_t now_ms)
{
  f->state    = FACE_IDLE;
  f->state_ms = now_ms;
  f->now_ms   = now_ms;

  target_pose(FACE_IDLE, now_ms, &f->pose);
  f->blend_from = f->pose;
}

void face_set_state(struct face *f, enum face_state state, uint32_t now_ms)
{
  /* Blend out of whatever is on screen right now, not out of the previous
   * state's target, so interrupting a blend does not snap.
   */

  f->blend_from = f->pose;
  f->state      = state;
  f->state_ms   = now_ms;
}

void face_tick(struct face *f, uint32_t now_ms)
{
  struct face_pose to;
  uint32_t elapsed;
  int32_t frac;

  /* The clock can go backwards across a reset or an NTP correction.  Treat
   * that as no time having passed rather than wrapping into a long blend.
   */

  elapsed = now_ms >= f->state_ms ? now_ms - f->state_ms : 0;
  frac    = elapsed >= FACE_BLEND_MS ? FACE_UNIT
            : (int32_t)((elapsed * FACE_UNIT) / FACE_BLEND_MS);

  target_pose(f->state, now_ms, &to);

  f->pose.eye_open_l  = mix(f->blend_from.eye_open_l, to.eye_open_l, frac);
  f->pose.eye_open_r  = mix(f->blend_from.eye_open_r, to.eye_open_r, frac);
  f->pose.pupil_x     = mix(f->blend_from.pupil_x, to.pupil_x, frac);
  f->pose.pupil_y     = mix(f->blend_from.pupil_y, to.pupil_y, frac);
  f->pose.brow        = mix(f->blend_from.brow, to.brow, frac);
  f->pose.mouth_curve = mix(f->blend_from.mouth_curve, to.mouth_curve, frac);
  f->pose.mouth_open  = mix(f->blend_from.mouth_open, to.mouth_open, frac);
  f->pose.glow        = mix(f->blend_from.glow, to.glow, frac);

  f->now_ms = now_ms;
}

int face_state_from_name(const char *name, enum face_state *state)
{
  int i;

  if (name == NULL || name[0] == '\0')
    {
      return -1;
    }

  for (i = 0; i < FACE_NSTATES; i++)
    {
      if (strcmp(name, g_state_names[i]) == 0)
        {
          *state = (enum face_state)i;
          return 0;
        }
    }

  return -1;
}

const char *face_state_name(enum face_state state)
{
  if (state < 0 || state >= FACE_NSTATES)
    {
      return "idle";
    }

  return g_state_names[state];
}
