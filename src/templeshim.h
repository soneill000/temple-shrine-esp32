// templeshim.h — a faithful TempleOS Gr* API on top of an RGB565
// off-screen framebuffer. Foundation for scene ports that need to look
// like Terry's original — not just play like it.
//
// Convention: scene code passes coordinates in Terry's native 640x480
// space. The shim halves them internally so they fit our 320x240
// panel. That way scene code stays line-by-line-close to Terry's
// HolyC without every port having to remember to /2.
//
// Colors are TempleOS palette indices (0-15). The shim's palette table
// maps to RGB565 on write.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "palette.h"

// --- Terry's DC (drawing context) flags we care about ---
#define DCF_TRANSFORMATION   0x01u
#define DCF_SYMMETRY         0x02u
#define DCF_JUST_MIRROR      0x04u

// --- CDC — Terry's drawing context, minimum viable subset ---
typedef struct {
    uint16_t *fb;      // target framebuffer (native RGB565)
    int       fb_w;    // framebuffer pixel width
    int       fb_h;    // framebuffer pixel height
    int       scale;   // Terry-coord -> fb-coord divisor (1 for 1:1, 2 for 640x480 code on 320x240)

    color_t   color;   // current draw color (palette index)
    int       thick;   // current line thickness in framebuffer pixels
    uint32_t  flags;   // DCF_* bits

    float     r[16];   // 4x4 transform matrix (row-major)
} CDC;

// --- Global "main window" DC that scenes normally draw into ---
extern CDC gr_dc;

// --- Init: point gr_dc at a caller-owned framebuffer ---
void CDCInit(uint16_t *fb, int fb_w, int fb_h, int scale);

// --- Present the current framebuffer to the panel ---
void DCPresent(CDC *dc);

// --- Fill the DC with the current color (equivalent to Terry's DCFill) ---
void DCFill(CDC *dc);

// --- 4x4 matrix ops (row-major, float). Enough for 2.5D pseudo-
//     perspective as used by HorebBMP. ---
void Mat4x4IdentEqu(float m[16]);
void Mat4x4RotX(float m[16], float ang);
void Mat4x4RotY(float m[16], float ang);
void Mat4x4RotZ(float m[16], float ang);
void Mat4x4Scale(float m[16], float s);
void Mat4x4Translate(float m[16], float x, float y, float z);
// Multiply a point (x, y, z) in-place by the matrix.
void Mat4x4MulXYZ(const float m[16], float *x, float *y, float *z);

// --- Primitives (coords in Terry space; shim scales internally) ---
void GrPlot(CDC *dc, int x, int y);
void GrPlot3(CDC *dc, int x, int y, int z);
void GrLine(CDC *dc, int x1, int y1, int x2, int y2);
void GrLine3(CDC *dc, int x1, int y1, int z1, int x2, int y2, int z2);
void GrRect(CDC *dc, int x, int y, int w, int h);
void GrFillRect(CDC *dc, int x, int y, int w, int h);
void GrCircle(CDC *dc, int cx, int cy, int r);
void GrFillCircle(CDC *dc, int cx, int cy, int r);
void GrFloodFill(CDC *dc, int x, int y);
void GrPrint(CDC *dc, int x, int y, const char *s);
// Read a pixel back — used by Terry's flood-fill-leak check pattern.
color_t GrPeek(CDC *dc, int x, int y);

// --- Sprite3: walk a CSprite command stream, apply the DC state
//     (color/thick/transform) to each op, rasterize. This is the whole
//     point of the shim — scene code just calls Sprite3 and the sprite
//     lands with correct color, thickness, and transform. ---
void Sprite3(CDC *dc, int x, int y, int z,
             const uint8_t *stream, unsigned size);
