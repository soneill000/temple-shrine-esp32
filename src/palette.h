// palette.h — the canonical 16-color IBM CGA / VGA palette that TempleOS
// uses everywhere. In-game code refers to colors by INDEX (0-15); the
// display driver looks them up in this table to get the RGB565 word for
// the ILI9341.
//
// Hex sources are the standard CGA text-mode intensities:
//   0x00 for "off", 0xAA for "on" (dim), 0x55/0xFF for "bright".
// Brown (6) is the classic CGA irregularity — R=AA, G=55, B=00.

#pragma once

#include <stdint.h>

typedef enum {
    C_BLACK         = 0,
    C_BLUE          = 1,
    C_GREEN         = 2,
    C_CYAN          = 3,
    C_RED           = 4,
    C_MAGENTA       = 5,
    C_BROWN         = 6,
    C_LTGRAY        = 7,
    C_DKGRAY        = 8,
    C_LTBLUE        = 9,
    C_LTGREEN       = 10,
    C_LTCYAN        = 11,
    C_LTRED         = 12,
    C_LTMAGENTA     = 13,
    C_YELLOW        = 14,
    C_WHITE         = 15,
} color_t;

// RGB565 (native little-endian; display driver byte-swaps on the wire).
static const uint16_t PAL_RGB565[16] = {
    /*  0 BLACK       #000000 */ 0x0000,
    /*  1 BLUE        #0000AA */ 0x0015,
    /*  2 GREEN       #00AA00 */ 0x0540,
    /*  3 CYAN        #00AAAA */ 0x0555,
    /*  4 RED         #AA0000 */ 0xA800,
    /*  5 MAGENTA     #AA00AA */ 0xA815,
    /*  6 BROWN       #AA5500 */ 0xAAA0,
    /*  7 LT GRAY     #AAAAAA */ 0xAD55,
    /*  8 DK GRAY     #555555 */ 0x52AA,
    /*  9 LT BLUE     #5555FF */ 0x52BF,
    /* 10 LT GREEN    #55FF55 */ 0x57EA,
    /* 11 LT CYAN     #55FFFF */ 0x57FF,
    /* 12 LT RED      #FF5555 */ 0xFAAA,
    /* 13 LT MAGENTA  #FF55FF */ 0xFABF,
    /* 14 YELLOW      #FFFF55 */ 0xFFEA,
    /* 15 WHITE       #FFFFFF */ 0xFFFF,
};

// Semantic aliases matching TempleOS default document colors.
#define C_BG           C_BLUE       // canonical "TempleOS blue"
#define C_FG           C_WHITE
#define C_HEADING      C_YELLOW
#define C_LINK         C_LTCYAN
#define C_ACCENT       C_LTGREEN
#define C_WARNING      C_LTRED
#define C_DIM          C_LTGRAY
#define C_CURSOR       C_LTMAGENTA
