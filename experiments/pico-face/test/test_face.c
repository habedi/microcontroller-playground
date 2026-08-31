/****************************************************************************
 * experiments/pico-face/test/test_face.c
 *
 * Host tests for the expression state machine.  Plain C and no framework, so
 * a bare cc is enough to run them.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include "face.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_checks;
static int g_failures;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void check(int ok, const char *what)
{
  g_checks++;
  if (!ok)
    {
      g_failures++;
      printf("  FAIL  %s\n", what);
    }
}

/* Advance the face to a given time in small steps, the way the render loop
 * does, so a test never depends on one big jump.
 */

static void advance(struct face *f, uint32_t from_ms, uint32_t to_ms)
{
  uint32_t t;

  for (t = from_ms; t < to_ms; t += 16)
    {
      face_tick(f, t);
    }

  face_tick(f, to_ms);
}

static int in_range(int16_t v, int lo, int hi)
{
  return v >= lo && v <= hi;
}

/* Every field of every pose has to stay inside its declared range, whatever
 * the state and whatever the time.  This is the one test that would catch an
 * overflow in the blend arithmetic.
 */

static void test_ranges_always_hold(void)
{
  struct face f;
  int s;

  for (s = 0; s < FACE_NSTATES; s++)
    {
      uint32_t t;

      face_init(&f, 0);
      face_set_state(&f, (enum face_state)s, 0);

      for (t = 0; t <= 12000; t += 13)
        {
          face_tick(&f, t);

          check(in_range(f.pose.eye_open_l, 0, FACE_UNIT), "eye_open_l range");
          check(in_range(f.pose.eye_open_r, 0, FACE_UNIT), "eye_open_r range");
          check(in_range(f.pose.pupil_x, -FACE_UNIT, FACE_UNIT), "pupil_x range");
          check(in_range(f.pose.pupil_y, -FACE_UNIT, FACE_UNIT), "pupil_y range");
          check(in_range(f.pose.brow, -FACE_UNIT, FACE_UNIT), "brow range");
          check(in_range(f.pose.mouth_curve, -FACE_UNIT, FACE_UNIT), "mouth_curve range");
          check(in_range(f.pose.mouth_open, 0, FACE_UNIT), "mouth_open range");
          check(in_range(f.pose.glow, 0, FACE_UNIT), "glow range");
        }
    }
}

static void test_init_is_idle_and_awake(void)
{
  struct face f;

  face_init(&f, 0);

  check(f.state == FACE_IDLE, "init state is idle");
  check(f.pose.eye_open_l > FACE_UNIT / 2, "init eyes are open");
  check(f.pose.eye_open_r > FACE_UNIT / 2, "init eyes are open");
}

/* A state change must not snap.  Just after the change the pose still has to
 * resemble the old one, and after the blend window it has to have arrived.
 */

static void test_state_change_blends(void)
{
  struct face a;
  struct face b;

  face_init(&a, 0);
  advance(&a, 0, 3000);

  b = a;
  face_set_state(&b, FACE_WAITING, 3000);
  face_tick(&b, 3000);

  check(b.pose.brow - a.pose.brow < FACE_UNIT / 4,
        "brow has not jumped at the instant of the change");

  advance(&b, 3000, 3000 + FACE_BLEND_MS);
  check(b.pose.brow > FACE_UNIT / 3, "brow is raised once the blend is done");
}

/* Idle has to blink: eyes shut at some point and are open at others, and the
 * shut part is a small fraction of the cycle.
 */

static void test_idle_blinks(void)
{
  struct face f;
  uint32_t t;
  int shut = 0;
  int open = 0;

  face_init(&f, 0);

  for (t = 0; t <= 20000; t += 20)
    {
      face_tick(&f, t);

      if (f.pose.eye_open_l < FACE_UNIT / 5)
        {
          shut++;
        }
      else if (f.pose.eye_open_l > (FACE_UNIT * 4) / 5)
        {
          open++;
        }
    }

  check(shut > 0, "idle closes the eyes at least once");
  check(open > shut * 4, "idle spends far more time open than shut");
}

/* Working sweeps the pupils.  The test asks only that they move both ways,
 * not for any particular path.
 */

static void test_working_sweeps_pupils(void)
{
  struct face f;
  uint32_t t;
  int16_t lo = FACE_UNIT;
  int16_t hi = -FACE_UNIT;

  face_init(&f, 0);
  face_set_state(&f, FACE_WORKING, 0);

  for (t = 0; t <= 8000; t += 20)
    {
      face_tick(&f, t);

      if (f.pose.pupil_x < lo)
        {
          lo = f.pose.pupil_x;
        }

      if (f.pose.pupil_x > hi)
        {
          hi = f.pose.pupil_x;
        }
    }

  check(lo < -FACE_UNIT / 4, "pupils reach the left");
  check(hi > FACE_UNIT / 4, "pupils reach the right");
}

static void test_waiting_pulses(void)
{
  struct face f;
  uint32_t t;
  int16_t lo = FACE_UNIT;
  int16_t hi = 0;

  face_init(&f, 0);
  face_set_state(&f, FACE_WAITING, 0);
  advance(&f, 0, FACE_BLEND_MS);

  for (t = FACE_BLEND_MS; t <= 8000; t += 20)
    {
      face_tick(&f, t);

      if (f.pose.glow < lo)
        {
          lo = f.pose.glow;
        }

      if (f.pose.glow > hi)
        {
          hi = f.pose.glow;
        }
    }

  check(hi - lo > FACE_UNIT / 3, "waiting pulses the glow");
}

/* The three remaining states have to be distinguishable from idle by the one
 * feature that names them.
 */

static void test_states_look_different(void)
{
  struct face idle;
  struct face f;

  face_init(&idle, 0);
  face_tick(&idle, 0);

  face_init(&f, 0);
  face_set_state(&f, FACE_EDITING, 0);
  advance(&f, 0, FACE_BLEND_MS);
  check(f.pose.pupil_y > FACE_UNIT / 4, "editing looks down");

  face_init(&f, 0);
  face_set_state(&f, FACE_FAILED, 0);
  advance(&f, 0, FACE_BLEND_MS);
  check(f.pose.eye_open_l < idle.pose.eye_open_l, "failed squints");
  check(f.pose.mouth_curve < 0, "failed does not smile");

  face_init(&f, 0);
  face_set_state(&f, FACE_DONE, 0);
  advance(&f, 0, FACE_BLEND_MS);
  check(f.pose.mouth_curve > FACE_UNIT / 3, "done smiles");
}

static void test_name_lookup(void)
{
  enum face_state s;

  check(face_state_from_name("idle", &s) == 0 && s == FACE_IDLE, "parse idle");
  check(face_state_from_name("working", &s) == 0 && s == FACE_WORKING, "parse working");
  check(face_state_from_name("editing", &s) == 0 && s == FACE_EDITING, "parse editing");
  check(face_state_from_name("waiting", &s) == 0 && s == FACE_WAITING, "parse waiting");
  check(face_state_from_name("failed", &s) == 0 && s == FACE_FAILED, "parse failed");
  check(face_state_from_name("done", &s) == 0 && s == FACE_DONE, "parse done");

  check(face_state_from_name("sleeping", &s) == -1, "reject unknown name");
  check(face_state_from_name("", &s) == -1, "reject empty name");

  check(strcmp(face_state_name(FACE_WORKING), "working") == 0, "name of working");
}

/* A tick that does not advance the clock must change nothing, and a clock
 * that jumps backwards must not wedge the animation.  Both happen on a real
 * board.
 */

static void test_odd_clocks(void)
{
  struct face f;
  struct face again;

  face_init(&f, 1000);
  face_set_state(&f, FACE_WORKING, 1000);
  advance(&f, 1000, 2000);

  again = f;
  face_tick(&again, 2000);
  check(memcmp(&again.pose, &f.pose, sizeof(f.pose)) == 0,
        "a repeated tick is idempotent");

  face_tick(&f, 500);
  check(in_range(f.pose.eye_open_l, 0, FACE_UNIT), "backwards clock stays in range");

  advance(&f, 500, 4000);
  check(in_range(f.pose.pupil_x, -FACE_UNIT, FACE_UNIT),
        "animation recovers after a backwards clock");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  test_init_is_idle_and_awake();
  test_state_change_blends();
  test_idle_blinks();
  test_working_sweeps_pupils();
  test_waiting_pulses();
  test_states_look_different();
  test_name_lookup();
  test_odd_clocks();
  test_ranges_always_hold();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
