/****************************************************************************
 * experiments/pico-face/src/face_input.c
 *
 * Turns button masks into actions.  No board code, so the host tests cover
 * every path through it.
 *
 ****************************************************************************/

#include <stddef.h>

#include "face_input.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct binding
{
  int bit;
  enum face_action action;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Checked in order, so this is also the precedence when two buttons go down
 * in the same poll.
 */

static const struct binding g_bindings[] =
{
  { FACE_BTN_LEFT,  FACE_ACT_PRESET_PREV },
  { FACE_BTN_RIGHT, FACE_ACT_PRESET_NEXT },
  { FACE_BTN_UP,    FACE_ACT_BRIGHT_UP },
  { FACE_BTN_DOWN,  FACE_ACT_BRIGHT_DOWN },
  { FACE_BTN_A,     FACE_ACT_HOLD },
  { FACE_BTN_B,     FACE_ACT_PALETTE },
  { FACE_BTN_X,     FACE_ACT_SPEED },
  { FACE_BTN_Y,     FACE_ACT_OVERLAY },
};

#define NBINDINGS ((int)(sizeof(g_bindings) / sizeof(g_bindings[0])))

/****************************************************************************
 * Public Functions
 ****************************************************************************/

enum face_action face_input_action(uint32_t now, uint32_t prev)
{
  /* Only bits that went from clear to set count, which is what turns a held
   * button into one action rather than one per poll.
   */

  uint32_t pressed = now & ~prev;
  int i;

  for (i = 0; i < NBINDINGS; i++)
    {
      if (pressed & FACE_BTN_BIT(g_bindings[i].bit))
        {
          return g_bindings[i].action;
        }
    }

  return FACE_ACT_NONE;
}

const char *face_action_name(enum face_action action)
{
  switch (action)
    {
      case FACE_ACT_NONE:        return "none";
      case FACE_ACT_PRESET_NEXT: return "preset next";
      case FACE_ACT_PRESET_PREV: return "preset prev";
      case FACE_ACT_BRIGHT_UP:   return "brighter";
      case FACE_ACT_BRIGHT_DOWN: return "dimmer";
      case FACE_ACT_HOLD:        return "hold";
      case FACE_ACT_PALETTE:     return "palette";
      case FACE_ACT_SPEED:       return "speed";
      case FACE_ACT_OVERLAY:     return "overlay";
      default:                   return "?";
    }
}
