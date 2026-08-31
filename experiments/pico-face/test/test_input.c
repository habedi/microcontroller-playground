/****************************************************************************
 * experiments/pico-face/test/test_input.c
 *
 * Host tests for the button mapping.  This is the part of the panel controls
 * that has no board in it, and it is where the edge detection lives, so it
 * is worth testing away from the hardware.
 *
 ****************************************************************************/

#include <stdio.h>

#include "face_input.h"

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

static void expect(uint32_t now, uint32_t prev, enum face_action want,
                   const char *what)
{
  enum face_action got = face_input_action(now, prev);

  g_checks++;
  if (got != want)
    {
      g_failures++;
      printf("  FAIL  %s: got %s, want %s\n", what,
             face_action_name(got), face_action_name(want));
    }
}

/****************************************************************************
 * Tests
 ****************************************************************************/

static void test_each_button_maps(void)
{
  expect(FACE_BTN_BIT(FACE_BTN_LEFT), 0, FACE_ACT_PRESET_PREV, "left");
  expect(FACE_BTN_BIT(FACE_BTN_RIGHT), 0, FACE_ACT_PRESET_NEXT, "right");
  expect(FACE_BTN_BIT(FACE_BTN_UP), 0, FACE_ACT_BRIGHT_UP, "up");
  expect(FACE_BTN_BIT(FACE_BTN_DOWN), 0, FACE_ACT_BRIGHT_DOWN, "down");
  expect(FACE_BTN_BIT(FACE_BTN_A), 0, FACE_ACT_HOLD, "A");
  expect(FACE_BTN_BIT(FACE_BTN_B), 0, FACE_ACT_PALETTE, "B");
  expect(FACE_BTN_BIT(FACE_BTN_X), 0, FACE_ACT_SPEED, "X");
  expect(FACE_BTN_BIT(FACE_BTN_Y), 0, FACE_ACT_OVERLAY, "Y");
}

/* Nothing pressed, and nothing newly pressed, both mean no action. */

static void test_idle_is_quiet(void)
{
  expect(0, 0, FACE_ACT_NONE, "nothing pressed");
  expect(FACE_BTN_BIT(FACE_BTN_A), FACE_BTN_BIT(FACE_BTN_A),
         FACE_ACT_NONE, "A still held");
}

/* The whole point of the edge detection: a button held across many polls
 * fires once, not once per poll.
 */

static void test_hold_does_not_repeat(void)
{
  uint32_t prev = 0;
  uint32_t held = FACE_BTN_BIT(FACE_BTN_RIGHT);
  int fired = 0;
  int i;

  for (i = 0; i < 20; i++)
    {
      if (face_input_action(held, prev) != FACE_ACT_NONE)
        {
          fired++;
        }

      prev = held;
    }

  check(fired == 1, "a held button fires exactly once");
}

/* Releasing and pressing again is a second action. */

static void test_release_then_press_fires_again(void)
{
  uint32_t bit = FACE_BTN_BIT(FACE_BTN_B);

  expect(bit, 0, FACE_ACT_PALETTE, "first press");
  expect(0, bit, FACE_ACT_NONE, "release is not an action");
  expect(bit, 0, FACE_ACT_PALETTE, "second press");
}

/* Two buttons in one poll resolve by the documented precedence rather than
 * by whichever bit happens to be lower.
 */

static void test_precedence_is_stable(void)
{
  uint32_t both = FACE_BTN_BIT(FACE_BTN_LEFT) | FACE_BTN_BIT(FACE_BTN_Y);

  expect(both, 0, FACE_ACT_PRESET_PREV, "left beats Y");
}

/* The joystick press has no binding yet, so it must stay silent rather than
 * falling through to whatever is first in the table.
 */

static void test_unbound_button_is_silent(void)
{
  expect(FACE_BTN_BIT(FACE_BTN_PRESS), 0, FACE_ACT_NONE,
         "the joystick press is unbound");
}

/* Bits above the ones the panel uses must be ignored, since a wider mask
 * from a board with more buttons should not invent actions.
 */

static void test_unknown_bits_ignored(void)
{
  expect(0xffff0000u, 0, FACE_ACT_NONE, "high bits do nothing");
}

static void test_every_action_is_named(void)
{
  int a;

  for (a = FACE_ACT_NONE; a <= FACE_ACT_OVERLAY; a++)
    {
      const char *n = face_action_name((enum face_action)a);

      check(n != NULL && n[0] != '\0' && n[0] != '?', "action is named");
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  test_each_button_maps();
  test_idle_is_quiet();
  test_hold_does_not_repeat();
  test_release_then_press_fires_again();
  test_precedence_is_stable();
  test_unbound_button_is_silent();
  test_unknown_bits_ignored();
  test_every_action_is_named();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
