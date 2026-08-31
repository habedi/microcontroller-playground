/****************************************************************************
 * experiments/pico-face/test/test_sprite.c
 *
 * Host tests for the sprite data, the palettes, and the preset table.  The
 * art is the part most likely to break silently, because a miscounted row
 * shifts every pixel after it without failing anything, so most of this file
 * is about the data rather than the drawing.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "face_hero_art.h"
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

/* Every row of every hero pose has to be exactly the declared width, and
 * hold only characters the palette can resolve.  A short row would otherwise
 * read past its end.
 */

static void test_hero_art_is_well_formed(void)
{
  int i;

  for (i = 0; i < FACE_NSTATES; i++)
    {
      const struct face_sprite *spr = &g_hero_poses[i];
      int y;

      check(spr->w > 0 && spr->h > 0, "hero sprite has a size");
      check(spr->rows != NULL, "hero sprite has rows");

      for (y = 0; y < spr->h; y++)
        {
          const char *row = spr->rows[y];
          size_t len = strlen(row);
          int x;
          int ok = 1;

          if (len != (size_t)spr->w)
            {
              printf("  pose %d row %d is %u wide, want %d\n",
                     i, y, (unsigned)len, spr->w);
            }

          check(len == (size_t)spr->w, "hero row is the declared width");

          for (x = 0; x < (int)len; x++)
            {
              if (row[x] != '.' && face_sprite_index(row[x]) < 0)
                {
                  ok = 0;
                }
            }

          check(ok, "hero row holds only palette characters");
        }
    }
}

/* A transparent pixel and a palette pixel have to be told apart the same way
 * everywhere, so this pins the mapping down rather than trusting it.
 */

static void test_sprite_index(void)
{
  check(face_sprite_index('.') < 0, "a dot is transparent");
  check(face_sprite_index(' ') < 0, "a space is transparent");
  check(face_sprite_index('x') < 0, "an unknown letter is transparent");
  check(face_sprite_index('0') == 0, "0 is the first palette entry");
  check(face_sprite_index('9') == 9, "9 is the tenth");
  check(face_sprite_index('a') == 10, "a follows 9");
  check(face_sprite_index('f') == 15, "f is the last");
}

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

/* A sprite placed so that it hangs off every edge must not write outside the
 * surface.  Run under the address sanitizer this is the real check.
 */

static void test_blit_clips(void)
{
  struct face_surface surf = panel();
  const struct face_palette *pal = face_palette(0);
  int offs[] = { -200, -1, 0, 1, 200 };
  size_t i;
  size_t j;

  for (i = 0; i < sizeof(offs) / sizeof(offs[0]); i++)
    {
      for (j = 0; j < sizeof(offs) / sizeof(offs[0]); j++)
        {
          face_sprite_blit(&surf, &g_hero_poses[0], pal, offs[i], offs[j], 7);
        }
    }

  check(1, "blitting off every edge stayed in bounds");
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  test_hero_art_is_well_formed();
  test_sprite_index();
  test_palettes();
  test_preset_wrap();
  test_every_preset_draws();
  test_blit_clips();
  test_tiny_surface();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
