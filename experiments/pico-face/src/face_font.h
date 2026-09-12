/****************************************************************************
 * experiments/pico-face/src/face_font.h
 *
 * The overlay font.  Five by seven pixels, uppercase only, with the digits
 * and the few symbols the overlay needs.
 *
 ****************************************************************************/

#ifndef __EXPERIMENTS_PICO_FACE_SRC_FACE_FONT_H
#define __EXPERIMENTS_PICO_FACE_SRC_FACE_FONT_H

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FACE_FONT_W 5
#define FACE_FONT_H 7

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* The characters the font covers, in the same order as g_font_rows. */

extern const char g_font_chars[];

/* One byte per row, the low five bits used, most significant bit leftmost. */

extern const uint8_t g_font_rows[][FACE_FONT_H];

#endif /* __EXPERIMENTS_PICO_FACE_SRC_FACE_FONT_H */
