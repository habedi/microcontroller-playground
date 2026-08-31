/****************************************************************************
 * experiments/pico-face/test/test_render.c
 *
 * Host tests for the renderer.  The valuable one is simply that it never
 * writes outside the surface: the buffer is heap allocated so the address
 * sanitizer turns any overrun into a failure.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_render.h"

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

static int render_into(uint16_t *buf, int w, int h, enum face_state state,
                      uint32_t t)
{
  struct face_surface s;
  struct face_dirty d;
  struct face f;

  s.pixels = buf;
  s.width = w;
  s.height = h;
  s.stride_px = w;

  face_init(&f, 0);
  face_set_state(&f, state, 0);
  face_tick(&f, t);

  memset(buf, 0, (size_t)w * (size_t)h * sizeof(uint16_t));
  face_render(&s, &f.pose, &d);

  return d.w == w && d.h == h;
}

/* Every state, at many times, on several panel sizes.  Odd and small sizes
 * are where a clipping bug hides.
 */

static void test_never_writes_out_of_bounds(void)
{
  static const int sizes[][2] =
  {
    {240, 240}, {128, 160}, {64, 64}, {17, 23}, {320, 240}
  };

  size_t i;

  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
      int w = sizes[i][0];
      int h = sizes[i][1];
      uint16_t *buf = malloc((size_t)w * (size_t)h * sizeof(uint16_t));
      int s;

      check(buf != NULL, "allocation");
      if (buf == NULL)
        {
          return;
        }

      for (s = 0; s < FACE_NSTATES; s++)
        {
          uint32_t t;

          for (t = 0; t < 4000; t += 97)
            {
              check(render_into(buf, w, h, (enum face_state)s, t),
                    "dirty box covers the surface");
            }
        }

      free(buf);
    }
}

/* Two different expressions must not produce the same picture, otherwise the
 * whole point of the state machine is lost.
 */

static void test_states_differ_on_screen(void)
{
  const int w = 240;
  const int h = 240;
  size_t bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
  uint16_t *a = malloc(bytes);
  uint16_t *b = malloc(bytes);
  int i;
  int j;

  check(a != NULL && b != NULL, "allocation");
  if (a == NULL || b == NULL)
    {
      free(a);
      free(b);
      return;
    }

  for (i = 0; i < FACE_NSTATES; i++)
    {
      for (j = i + 1; j < FACE_NSTATES; j++)
        {
          render_into(a, w, h, (enum face_state)i, 500);
          render_into(b, w, h, (enum face_state)j, 500);
          check(memcmp(a, b, bytes) != 0, "two states draw differently");
        }
    }

  free(a);
  free(b);
}

/* A shut eye has to change the picture, which is the one thing a blink is. */

static void test_blink_changes_the_picture(void)
{
  const int w = 240;
  const int h = 240;
  size_t bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
  uint16_t *open_buf = malloc(bytes);
  uint16_t *shut_buf = malloc(bytes);
  struct face_surface s;
  struct face_dirty d;
  struct face_pose p;

  check(open_buf != NULL && shut_buf != NULL, "allocation");
  if (open_buf == NULL || shut_buf == NULL)
    {
      free(open_buf);
      free(shut_buf);
      return;
    }

  s.width = w;
  s.height = h;
  s.stride_px = w;

  memset(&p, 0, sizeof(p));
  p.eye_open_l = FACE_UNIT;
  p.eye_open_r = FACE_UNIT;
  p.glow = 400;

  s.pixels = open_buf;
  face_render(&s, &p, &d);

  p.eye_open_l = 0;
  p.eye_open_r = 0;
  s.pixels = shut_buf;
  face_render(&s, &p, &d);

  check(memcmp(open_buf, shut_buf, bytes) != 0, "shut eyes look different");

  free(open_buf);
  free(shut_buf);
}


/* A rounded rectangle has to be widest across its middle.  The first version
 * of round_rect() had the corner arithmetic inverted, which pinched the waist
 * and drew an hourglass instead, and every test above still passed.  This is
 * the cheapest check that catches it.
 */

static void test_eyes_are_widest_in_the_middle(void)
{
  const int w = 240;
  const int h = 240;
  uint16_t *buf = malloc((size_t)w * (size_t)h * sizeof(uint16_t));
  struct face_surface s;
  struct face_dirty d;
  struct face_pose p;
  uint16_t bg;
  int best_row = 0;
  int best_count = -1;
  int y;

  check(buf != NULL, "allocation");
  if (buf == NULL)
    {
      return;
    }

  s.pixels = buf;
  s.width = w;
  s.height = h;
  s.stride_px = w;

  /* Eyes fully open and brows resting, so the only thing in the band of rows
   * examined below is the pair of eyes.
   */

  memset(&p, 0, sizeof(p));
  p.eye_open_l = FACE_UNIT;
  p.eye_open_r = FACE_UNIT;
  p.glow = 400;

  face_render(&s, &p, &d);
  bg = buf[0];

  /* The eyes sit at 40 percent of the height and are 27 percent tall, so rows
   * 64 to 128 hold them and nothing else.
   */

  for (y = 64; y <= 128; y++)
    {
      int count = 0;
      int x;

      for (x = 0; x < w; x++)
        {
          if (buf[(size_t)y * (size_t)w + (size_t)x] != bg)
            {
              count++;
            }
        }

      if (count > best_count)
        {
          best_count = count;
          best_row = y;
        }
    }

  check(best_count > 0, "the eyes drew something");
  check(best_row > 80 && best_row < 112,
        "the widest row is near the middle of the eye, not at its edge");

  free(buf);
}

/* A smile has to put the middle of the mouth below its corners, because y
 * grows downwards.  Getting this backwards drew a frown for done and a smile
 * for failed.
 */

static void test_smile_points_the_right_way(void)
{
  const int w = 240;
  const int h = 240;
  uint16_t *buf = malloc((size_t)w * (size_t)h * sizeof(uint16_t));
  struct face_surface s;
  struct face_dirty d;
  struct face_pose p;
  uint16_t bg;
  int mid_y = -1;
  int end_y = -1;
  int y;

  check(buf != NULL, "allocation");
  if (buf == NULL)
    {
      return;
    }

  s.pixels = buf;
  s.width = w;
  s.height = h;
  s.stride_px = w;

  memset(&p, 0, sizeof(p));
  p.mouth_curve = FACE_UNIT;
  p.glow = 400;

  face_render(&s, &p, &d);
  bg = buf[0];

  /* Topmost drawn pixel in the middle column, and in a column near the left
   * end of the mouth.  The mouth is 30 percent of the width, centred.
   */

  for (y = h / 2; y < h; y++)
    {
      if (mid_y < 0 && buf[(size_t)y * (size_t)w + (size_t)(w / 2)] != bg)
        {
          mid_y = y;
        }

      if (end_y < 0 && buf[(size_t)y * (size_t)w + (size_t)(w / 2 - 32)] != bg)
        {
          end_y = y;
        }
    }

  check(mid_y > 0 && end_y > 0, "the mouth drew something");
  check(mid_y > end_y, "a smile dips in the middle");

  free(buf);
}


/* Moving the pupil must not change which pixels are lit, only their colour.
 * The pupil rides inside the eye, so if it ever spills past the lid it lights
 * background pixels and reads as a dark blob hanging off the eye.
 */

static void test_pupil_stays_inside_the_eye(void)
{
  const int w = 240;
  const int h = 240;
  size_t count = (size_t)w * (size_t)h;
  uint16_t *centred = malloc(count * sizeof(uint16_t));
  uint16_t *moved = malloc(count * sizeof(uint16_t));
  struct face_surface s;
  struct face_dirty d;
  struct face_pose p;
  static const int16_t offsets[][2] =
  {
    {FACE_UNIT, 0}, {-FACE_UNIT, 0}, {0, FACE_UNIT}, {0, -FACE_UNIT},
    {FACE_UNIT, FACE_UNIT}, {-FACE_UNIT, -FACE_UNIT}
  };

  size_t k;
  int16_t open;

  check(centred != NULL && moved != NULL, "allocation");
  if (centred == NULL || moved == NULL)
    {
      free(centred);
      free(moved);
      return;
    }

  s.width = w;
  s.height = h;
  s.stride_px = w;

  /* Several lid openings, because a narrowed eye is where the pupil escapes. */

  for (open = FACE_UNIT; open >= 200; open -= 180)
    {
      uint16_t bg;
      size_t lit_centred = 0;
      size_t i;

      memset(&p, 0, sizeof(p));
      p.eye_open_l = open;
      p.eye_open_r = open;
      p.glow = 400;

      s.pixels = centred;
      face_render(&s, &p, &d);
      bg = centred[0];

      for (i = 0; i < count; i++)
        {
          if (centred[i] != bg)
            {
              lit_centred++;
            }
        }

      for (k = 0; k < sizeof(offsets) / sizeof(offsets[0]); k++)
        {
          size_t lit_moved = 0;

          p.pupil_x = offsets[k][0];
          p.pupil_y = offsets[k][1];

          s.pixels = moved;
          face_render(&s, &p, &d);

          for (i = 0; i < count; i++)
            {
              if (moved[i] != bg)
                {
                  lit_moved++;
                }
            }

          check(lit_moved == lit_centred,
                "the pupil lights no pixel outside the eye");
        }
    }

  free(centred);
  free(moved);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  test_never_writes_out_of_bounds();
  test_states_differ_on_screen();
  test_blink_changes_the_picture();
  test_eyes_are_widest_in_the_middle();
  test_smile_points_the_right_way();
  test_pupil_stays_inside_the_eye();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
