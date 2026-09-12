/****************************************************************************
 * experiments/pico-face/test/test_dirty.c
 *
 * Host tests for the change tracker.  The failure that matters is a change
 * left outside the reported box, because that is a stale pixel on the panel
 * that nothing will ever come back for.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "face_dirty.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define W 240
#define H 240

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_checks;
static int g_failures;

static uint16_t g_pixels[W * H];
static struct face_tracker g_tracker;

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

static struct face_surface surface(void)
{
  struct face_surface s;

  s.pixels = g_pixels;
  s.width = W;
  s.height = H;
  s.stride_px = W;
  return s;
}

static void fill(uint16_t colour)
{
  int i;

  for (i = 0; i < W * H; i++)
    {
      g_pixels[i] = colour;
    }
}

static void set(int x, int y, uint16_t colour)
{
  g_pixels[y * W + x] = colour;
}

static int inside(const struct face_dirty *d, int x, int y)
{
  return x >= d->x && x < d->x + d->w && y >= d->y && y < d->y + d->h;
}

/****************************************************************************
 * Tests
 ****************************************************************************/

static void test_first_frame_is_whole(void)
{
  struct face_surface s = surface();
  struct face_dirty d;

  face_tracker_reset(&g_tracker);
  fill(0x1234);
  face_tracker_scan(&g_tracker, &s, &d);

  check(d.x == 0 && d.y == 0 && d.w == W && d.h == H,
        "the first frame after a reset is the whole surface");
}

static void test_same_frame_is_empty(void)
{
  struct face_surface s = surface();
  struct face_dirty d;

  face_tracker_reset(&g_tracker);
  fill(0x1234);
  face_tracker_scan(&g_tracker, &s, &d);
  face_tracker_scan(&g_tracker, &s, &d);

  check(d.w == 0 && d.h == 0, "an unchanged frame has an empty box");
}

static void test_one_pixel(void)
{
  struct face_surface s = surface();
  struct face_dirty d;

  face_tracker_reset(&g_tracker);
  fill(0);
  face_tracker_scan(&g_tracker, &s, &d);

  set(100, 37, 0xffff);
  face_tracker_scan(&g_tracker, &s, &d);

  check(d.x == 100 && d.y == 37 && d.w == 2 && d.h == 1,
        "one changed pixel gives a box one row tall and one pair wide");

  set(101, 37, 0xeeee);
  face_tracker_scan(&g_tracker, &s, &d);
  check(d.x == 100 && d.w == 2, "its neighbour shares the pair");

  set(102, 37, 0xeeee);
  face_tracker_scan(&g_tracker, &s, &d);
  check(d.x == 102 && d.w == 2, "the next pixel is the next pair");
}

/* Two separate changes give one box that covers both, and nothing that
 * changed may fall outside it.
 */

static void test_two_changes_are_bounded(void)
{
  struct face_surface s = surface();
  struct face_dirty d;
  int x;
  int y;

  face_tracker_reset(&g_tracker);
  fill(0);
  face_tracker_scan(&g_tracker, &s, &d);

  for (y = 60; y < 90; y++)
    {
      for (x = 50; x < 90; x++)
        {
          set(x, y, 0x0f0f);
        }

      for (x = 150; x < 190; x++)
        {
          set(x, y, 0x0f0f);
        }
    }

  face_tracker_scan(&g_tracker, &s, &d);

  check(d.x == 50 && d.w == 140 && d.y == 60 && d.h == 30,
        "two eyes give one box from the left edge of one to the right edge "
        "of the other");
  check(inside(&d, 50, 60) && inside(&d, 189, 89),
        "both corners of the change are inside the box");
  check(!inside(&d, 49, 60) && !inside(&d, 50, 59),
        "the box does not start before the change");
}

/* The tracker remembers the frame it just saw, not the one before, so a
 * change that is then changed back is reported both times.
 */

static void test_reverting_is_a_change(void)
{
  struct face_surface s = surface();
  struct face_dirty d;

  face_tracker_reset(&g_tracker);
  fill(0);
  face_tracker_scan(&g_tracker, &s, &d);

  set(10, 10, 1);
  face_tracker_scan(&g_tracker, &s, &d);
  check(d.w == 2 && d.h == 1, "the change is seen");

  set(10, 10, 0);
  face_tracker_scan(&g_tracker, &s, &d);
  check(d.w == 2 && d.h == 1 && d.x == 10 && d.y == 10,
        "changing it back is seen too");
}

/* Every pixel that differs from the previous frame must be inside the box,
 * checked against a brute force diff over a spread of random changes.  A
 * hash collision would show up here as a missed pixel.
 */

static void test_no_change_escapes(void)
{
  struct face_surface s = surface();
  struct face_dirty d;
  static uint16_t before[W * H];
  uint32_t seed = 12345;
  int round;

  face_tracker_reset(&g_tracker);
  fill(0x2222);
  face_tracker_scan(&g_tracker, &s, &d);

  for (round = 0; round < 200; round++)
    {
      int n;
      int i;
      int escaped = 0;

      memcpy(before, g_pixels, sizeof(before));

      seed = seed * 1103515245u + 12345u;
      n = (int)(seed >> 16) % 40;

      for (i = 0; i < n; i++)
        {
          seed = seed * 1103515245u + 12345u;
          set((int)((seed >> 8) % W), (int)((seed >> 20) % H),
              (uint16_t)(seed >> 3));
        }

      face_tracker_scan(&g_tracker, &s, &d);

      for (i = 0; i < W * H; i++)
        {
          if (g_pixels[i] != before[i] && !inside(&d, i % W, i / W))
            {
              escaped = 1;
            }
        }

      check(!escaped, "every changed pixel is inside the box");
    }
}

/* A surface wider than the tracker can follow is pushed whole rather than
 * indexed past the end of the hash tables.
 */

static void test_oversize_surface_is_whole(void)
{
  static uint16_t big[(FACE_TRACK_MAX + 2) * 2];
  struct face_surface s;
  struct face_dirty d;

  s.pixels = big;
  s.width = FACE_TRACK_MAX + 2;
  s.height = 2;
  s.stride_px = s.width;

  face_tracker_reset(&g_tracker);
  face_tracker_scan(&g_tracker, &s, &d);
  face_tracker_scan(&g_tracker, &s, &d);

  check(d.w == s.width && d.h == 2, "an oversize surface is always whole");

  /* An odd width has no pair for its last column, so it is whole too. */

  s.width = 7;
  s.stride_px = 7;
  face_tracker_reset(&g_tracker);
  face_tracker_scan(&g_tracker, &s, &d);
  face_tracker_scan(&g_tracker, &s, &d);
  check(d.w == 7 && d.h == 2, "an odd width is always whole");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  test_first_frame_is_whole();
  test_same_frame_is_empty();
  test_one_pixel();
  test_two_changes_are_bounded();
  test_reverting_is_a_change();
  test_no_change_escapes();
  test_oversize_surface_is_whole();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
