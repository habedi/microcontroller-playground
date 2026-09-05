/****************************************************************************
 * experiments/pico-face/test/test_sprite.c
 *
 * Host tests for the palettes, the preset table, and the pixel art presets.
 * The vector preset has its own file; these cover the two drawn on a grid,
 * whose expression signs are the thing that has gone wrong before.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PANEL 240

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_checks;
static int g_failures;

static uint16_t g_pixels[PANEL * PANEL];

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

static struct face_surface panel(void)
{
  struct face_surface s;

  s.pixels = g_pixels;
  s.width = PANEL;
  s.height = PANEL;
  s.stride_px = PANEL;
  return s;
}

/****************************************************************************
 * Tests
 ****************************************************************************/

static void test_palettes(void)
{
  int i;

  for (i = 0; i < FACE_NPALETTES; i++)
    {
      const struct face_palette *pal = face_palette(i);

      check(pal != NULL, "palette exists");
      check(pal->name != NULL && pal->name[0] != '\0', "palette is named");
    }

  check(face_palette(-1) == face_palette(0), "a negative palette clamps");
  check(face_palette(FACE_NPALETTES) == face_palette(0),
        "a palette past the end clamps");
}

/* The wrap is what lets the joystick step off either end of the table. */

static void test_preset_wrap(void)
{
  int n = face_preset_count();

  check(n >= 3, "there are at least three presets");
  check(face_preset_wrap(0) == 0, "zero stays put");
  check(face_preset_wrap(n) == 0, "one past the end wraps to the front");
  check(face_preset_wrap(-1) == n - 1, "one before the front wraps to the end");
  check(face_preset_wrap(-n - 1) == n - 1, "a long way negative still wraps");

  for (int i = 0; i < n; i++)
    {
      const struct face_preset *p = face_preset(i);

      check(p->name != NULL && p->name[0] != '\0', "preset is named");
      check(p->render != NULL, "preset has a renderer");
    }
}

/* Every preset has to fill the surface it is handed and stay inside it.  The
 * sanitizer catches the overrun; this catches the preset that quietly draws
 * nothing.
 */

static void test_every_preset_draws(void)
{
  int p;

  for (p = 0; p < face_preset_count(); p++)
    {
      const struct face_preset *preset = face_preset(p);
      int s;

      for (s = 0; s < FACE_NSTATES; s++)
        {
          struct face_surface surf = panel();
          struct face_dirty dirty;
          struct face f;
          int different = 0;
          int i;

          memset(g_pixels, 0x5a, sizeof(g_pixels));

          face_init(&f, 0);
          face_set_state(&f, (enum face_state)s, 0);
          face_tick(&f, 500);

          preset->render(&surf, &f.pose, (enum face_state)s, 500, 0, &dirty);

          for (i = 0; i < PANEL * PANEL; i++)
            {
              if (g_pixels[i] != 0x5a5a)
                {
                  different = 1;
                  break;
                }
            }

          check(different, "preset drew something");
          check(dirty.x == 0 && dirty.y == 0, "dirty box starts at the origin");
          check(dirty.w == PANEL && dirty.h == PANEL,
                "dirty box covers the panel");
        }
    }
}

/* A panel smaller than one sprite pixel per block still has to be safe, since
 * the scale is worked out from the surface size.
 */

static void test_tiny_surface(void)
{
  uint16_t small[8 * 8];
  struct face_surface surf;
  struct face_dirty dirty;
  struct face f;
  int p;

  surf.pixels = small;
  surf.width = 8;
  surf.height = 8;
  surf.stride_px = 8;

  face_init(&f, 0);

  for (p = 0; p < face_preset_count(); p++)
    {
      memset(small, 0, sizeof(small));
      face_preset(p)->render(&surf, &f.pose, FACE_IDLE, 0, 0, &dirty);
    }

  check(1, "an eight pixel panel did not overrun");
}

/* Row of the first pixel of a colour in one column, or -1. */

static int first_row_of(const struct face_surface *s, int x, int y0, int y1,
                        uint16_t colour)
{
  int y;

  for (y = y0; y < y1; y++)
    {
      if (s->pixels[(size_t)y * (size_t)s->stride_px + (size_t)x] == colour)
        {
          return y;
        }
    }

  return -1;
}

/* The pixel portrait had both of its expression signs backwards: done
 * frowned and failed smiled, and the brows of waiting were angry.  These pin
 * the directions the same way test_render.c does for the vector face.  The
 * art is on a 48 grid, five panel pixels per cell, so a cell's column is
 * sampled at its middle.
 */

#define CELL(n) ((n) * 5 + 2)

static void test_pixel_smile_points_the_right_way(void)
{
  struct face_surface surf = panel();
  const struct face_palette *pal = face_palette(0);
  struct face_dirty dirty;
  struct face_pose p;
  int mid;
  int end;

  memset(&p, 0, sizeof(p));
  p.eye_open_l = FACE_UNIT;
  p.eye_open_r = FACE_UNIT;
  p.mouth_curve = FACE_UNIT;

  face_render_pixel(&surf, &p, FACE_IDLE, 0, 0, &dirty);

  /* The mouth is centred on cell 24 and runs from cell 18 to 30, below the
   * eyes and above the shoulders, so rows 150 to 199 hold only its outline
   * colour in those columns.
   */

  mid = first_row_of(&surf, CELL(24), 150, 200, pal->colour[1]);
  end = first_row_of(&surf, CELL(18), 150, 200, pal->colour[1]);
  check(mid > 0 && end > 0, "the pixel mouth drew something");
  check(mid > end, "a pixel smile dips in the middle");
}

static void test_pixel_brow_tilts_the_right_way(void)
{
  struct face_surface surf = panel();
  const struct face_palette *pal = face_palette(0);
  struct face_dirty dirty;
  struct face_pose p;
  int outer;
  int inner;

  memset(&p, 0, sizeof(p));
  p.eye_open_l = FACE_UNIT;
  p.eye_open_r = FACE_UNIT;

  /* The left eye is at cell 18 and its brow spans cells 15 to 21.  Between
   * the hair and the eye's white, rows 60 to 114, the brow colour appears
   * nowhere else in those columns.  A fully lowered brow reaches the eye's
   * outline row and no further.
   */

  p.brow = -FACE_UNIT;
  face_render_pixel(&surf, &p, FACE_IDLE, 0, 0, &dirty);
  outer = first_row_of(&surf, CELL(15), 60, 115, pal->colour[6]);
  inner = first_row_of(&surf, CELL(21), 60, 115, pal->colour[6]);
  check(outer > 0 && inner > 0, "a lowered pixel brow drew both ends");
  check(inner > outer, "a lowered pixel brow drops its inner end");

  p.brow = FACE_UNIT;
  face_render_pixel(&surf, &p, FACE_IDLE, 0, 0, &dirty);
  outer = first_row_of(&surf, CELL(15), 60, 115, pal->colour[6]);
  inner = first_row_of(&surf, CELL(21), 60, 115, pal->colour[6]);
  check(outer > 0 && inner > 0, "a raised pixel brow drew both ends");
  check(inner < outer, "a raised pixel brow lifts its inner end");
}

/* The crab is on a 40 grid, six panel pixels per cell. */

#define CRAB(n) ((n) * 6 + 3)

/* Same sign check for the crab.  Its mouth is centred on cell 20 and runs
 * from cell 15 to 25, and its outline is the only near black in those
 * columns between the glasses and the stubble.
 */

static void test_crab_smile_points_the_right_way(void)
{
  struct face_surface surf = panel();
  struct face_dirty dirty;
  struct face_pose p;
  uint16_t line;
  int mid;
  int end;

  memset(&p, 0, sizeof(p));
  p.eye_open_l = FACE_UNIT;
  p.eye_open_r = FACE_UNIT;
  p.mouth_curve = FACE_UNIT;

  face_render_crab(&surf, &p, FACE_IDLE, 0, 0, &dirty);

  /* The outline colour is read back from a glasses frame cell, so the test
   * does not have to know the crab's palette.
   */

  line = g_pixels[(size_t)CRAB(16) * PANEL + (size_t)CRAB(10)];

  mid = first_row_of(&surf, CRAB(20), CRAB(24), CRAB(32), line);
  end = first_row_of(&surf, CRAB(15), CRAB(24), CRAB(32), line);
  check(mid > 0 && end > 0, "the crab mouth drew something");
  check(mid > end, "a crab smile dips in the middle");
}

/* At rest the crab's brows are already down, which is the whole joke: with
 * a neutral pose its inner brow ends sit lower than its outer ones.
 */

static void test_crab_is_angrier_than_the_pose(void)
{
  struct face_surface surf = panel();
  struct face_dirty dirty;
  struct face_pose p;
  uint16_t line;
  int outer;
  int inner;

  memset(&p, 0, sizeof(p));
  p.eye_open_l = FACE_UNIT;
  p.eye_open_r = FACE_UNIT;

  face_render_crab(&surf, &p, FACE_IDLE, 0, 0, &dirty);
  line = g_pixels[(size_t)CRAB(16) * PANEL + (size_t)CRAB(10)];

  /* Left brow, cells 11 to 17, above the frame's top edge at cell 16. */

  outer = first_row_of(&surf, CRAB(11), CRAB(10), CRAB(16), line);
  inner = first_row_of(&surf, CRAB(17), CRAB(10), CRAB(16), line);
  check(outer > 0 && inner > 0, "the crab drew both brow ends");
  check(inner > outer, "a resting crab lowers its inner brow ends");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  test_palettes();
  test_preset_wrap();
  test_every_preset_draws();
  test_tiny_surface();
  test_pixel_smile_points_the_right_way();
  test_pixel_brow_tilts_the_right_way();
  test_crab_smile_points_the_right_way();
  test_crab_is_angrier_than_the_pose();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
