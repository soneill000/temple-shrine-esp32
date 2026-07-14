// sprite_decoder.c — walk a CSprite op stream extracted from a
// TempleOS .HC file and rasterize through the shrine shim.
//
// Coverage: SPT_END, SPT_COLOR, SPT_THICK, SPT_LINE, SPT_RECT,
// SPT_CIRCLE, SPT_ARROW, SPT_TRANSFORM_ON/OFF, SPT_DITHER_COLOR
// (approximated as SPT_COLOR).
//
// Deferred: SPT_FLOOD_FILL / SPT_FLOOD_FILL_NOT (needs a readable
// framebuffer we don't have on the panel), SPT_POLYGON (regular
// polygon rendering), SPT_MESH / SPT_SHIFTABLE_MESH (3D), SPT_BITMAP
// (handled separately via the bitmap header extractor), SPT_TEXT*.
//
// Coordinates in the op stream are signed 32-bit offsets from the
// caller's (cx, cy) anchor point — exactly how Terry positions sprites
// via Sprite3(dc, x, y, sprite).

#include "sprite_decoder.h"
#include "shrine.h"
#include "palette.h"

#include <stdint.h>
#include <stdbool.h>

static inline int32_t rd_i32(const uint8_t *b, int off)
{
    return (int32_t)((uint32_t)b[off]
                   | ((uint32_t)b[off + 1] <<  8)
                   | ((uint32_t)b[off + 2] << 16)
                   | ((uint32_t)b[off + 3] << 24));
}

// Draw a line at a given thickness — walk perpendicular offsets and
// stroke each. Cheap and reasonable for thick <= 4.
static void thick_line(int x1, int y1, int x2, int y2,
                       color_t c, int thick)
{
    if (thick < 1) thick = 1;
    if (thick > 5) thick = 5;
    int off = -(thick / 2);
    for (int i = 0; i < thick; i++) {
        int d = off + i;
        // Perpendicular offset — approximated as y offset for mostly-
        // horizontal, x offset for mostly-vertical.
        int adx = x2 > x1 ? x2 - x1 : x1 - x2;
        int ady = y2 > y1 ? y2 - y1 : y1 - y2;
        if (adx >= ady) {
            shrine_line(x1, y1 + d, x2, y2 + d, c);
        } else {
            shrine_line(x1 + d, y1, x2 + d, y2, c);
        }
    }
}

void sprite_render_stream(const uint8_t *stream, unsigned size,
                          int cx, int cy)
{
    if (!stream || size == 0) return;

    color_t cur = C_WHITE;
    int thick = 1;
    unsigned off = 0;
    int guard = 200;   // max ops before we bail

    while (off < size && guard-- > 0) {
        uint8_t op = stream[off];
        switch (op) {
        case SPT_END:
            return;

        case SPT_COLOR:
            if (off + 1 >= size) return;
            cur = (color_t)(stream[off + 1] & 15);
            off += 2;
            break;

        case SPT_DITHER_COLOR:
            // Approximated as the low byte of the dither word.
            if (off + 2 >= size) return;
            cur = (color_t)(stream[off + 1] & 15);
            off += 3;
            break;

        case SPT_THICK:
            if (off + 4 >= size) return;
            thick = rd_i32(stream, off + 1);
            if (thick < 1) thick = 1;
            off += 5;
            break;

        case SPT_TRANSFORM_ON:
        case SPT_TRANSFORM_OFF:
            off += 1;
            break;

        case SPT_PT:
        case SPT_SHIFT: {
            if (off + 8 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            shrine_pixel(cx + x, cy + y, cur);
            off += 9;
            break;
        }

        case SPT_LINE:
        case SPT_ARROW: {
            if (off + 16 >= size) return;
            int x1 = rd_i32(stream, off + 1);
            int y1 = rd_i32(stream, off + 5);
            int x2 = rd_i32(stream, off + 9);
            int y2 = rd_i32(stream, off + 13);
            thick_line(cx + x1, cy + y1, cx + x2, cy + y2, cur, thick);
            off += 17;
            break;
        }

        case SPT_RECT: {
            if (off + 16 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int w = rd_i32(stream, off + 9);
            int h = rd_i32(stream, off + 13);
            shrine_fill_rect(cx + x, cy + y, w, h, cur);
            off += 17;
            break;
        }

        case SPT_CIRCLE: {
            if (off + 12 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int r = rd_i32(stream, off + 9);
            shrine_circle(cx + x, cy + y, r, cur);
            off += 13;
            break;
        }

        case SPT_FLOOD_FILL:
        case SPT_FLOOD_FILL_NOT:
            // Not implemented — outline-only rendering.
            off += 9;
            break;

        default:
            // Unknown / unsupported op — stop rather than misinterpret
            // subsequent bytes.
            return;
        }
    }
}

// Older API kept for the header stub — thin wrapper.
void sprite_render(const sprite_ref_t *sp, int x, int y)
{
    if (sp) sprite_render_stream(sp->data, sp->size, x, y);
}

int sprite_scan_tail(const uint8_t *tail, uint32_t tail_size,
                     sprite_ref_t *out, int max_out)
{
    (void)tail; (void)tail_size; (void)out; (void)max_out;
    // Runtime parsing of the DolDoc envelope is deferred — the current
    // pipeline extracts sprites at build time via
    // tools/extract_vector_sprites.py and emits C arrays. This scan is
    // a no-op stub for now.
    return 0;
}
