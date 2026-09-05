/****************************************************************************
 * experiments/pico-face/src/face_input.h
 *
 * The panel's joystick and four buttons, turned into actions.
 *
 * The mapping is a pure function of the current and previous button masks,
 * so it builds and runs on the host under test/.  Opening and reading
 * /dev/buttons is left to face_main.c, which is the only part that needs a
 * board.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_INPUT_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_INPUT_H

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Bit positions in the mask read from /dev/buttons.  These follow the order
 * the buttons are declared in the board's board.h, not the GPIO numbers.
 */

#define FACE_BTN_UP     0
#define FACE_BTN_DOWN   1
#define FACE_BTN_LEFT   2
#define FACE_BTN_RIGHT  3
#define FACE_BTN_PRESS  4
#define FACE_BTN_A      5
#define FACE_BTN_B      6
#define FACE_BTN_X      7
#define FACE_BTN_Y      8

#define FACE_BTN_BIT(n) (1u << (n))

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum face_action
{
  FACE_ACT_NONE = 0,
  FACE_ACT_PRESET_NEXT,
  FACE_ACT_PRESET_PREV,
  FACE_ACT_BRIGHT_UP,
  FACE_ACT_BRIGHT_DOWN,
  FACE_ACT_HOLD,          /* A, toggles ignoring the state file */
  FACE_ACT_PALETTE,       /* B, next palette */
  FACE_ACT_SPEED,         /* X, next animation speed */
  FACE_ACT_OVERLAY        /* Y, toggles the debug overlay */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* The action for one poll.  Fires on the press rather than the release, and
 * only on the edge, so holding a button down does not repeat.
 *
 * There is no debounce here on purpose.  The caller polls every 100 ms and
 * contact bounce settles in a few milliseconds, so a sample taken that
 * slowly is already clean.
 *
 * When several buttons go down in the same poll, the first in the order
 * below wins.  Pressing two at once is not a gesture this needs to support.
 */

enum face_action face_input_action(uint32_t now, uint32_t prev);

/* The name of an action, for the console and for test failures. */

const char *face_action_name(enum face_action action);

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_INPUT_H */
