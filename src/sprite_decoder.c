// sprite_decoder.c — walk a CSprite op stream extracted from a
// TempleOS .HC file and rasterize.
//
// Two render modes:
//   sprite_render_stream(...)   → straight to the panel via shrine_*
//                                 primitives. Cheap, no flood fill.
//   sprite_render_buffered(...) → into a caller-owned RGB565 buffer,
//                                 then the caller blits the buffer to
//                                 the panel. Slower but supports flood
//                                 fill (which needs read-back that the
//                                 ILI9341 doesn't give us in practice).
//
// Op coverage matches Terry's small-game sprite usage. See disasm_sprite
// output for what's actually used per game.

#include "sprite_decoder.h"
#include "shrine.h"
#include "palette.h"
#include "hw.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ------------------------------------------------------------------
//  Shared helpers
// ------------------------------------------------------------------

static inline int32_t rd_i32(const uint8_t *b, int off)
{
    return (int32_t)((uint32_t)b[off]
                   | ((uint32_t)b[off + 1] <<  8)
                   | ((uint32_t)b[off + 2] << 16)
                   | ((uint32_t)b[off + 3] << 24));
}

// ------------------------------------------------------------------
//  Direct-to-panel (unbuffered) mode
// ------------------------------------------------------------------

static void thick_line(int x1, int y1, int x2, int y2,
                       color_t c, int thick)
{
    if (thick < 1) thick = 1;
    if (thick > 5) thick = 5;
    int off = -(thick / 2);
    for (int i = 0; i < thick; i++) {
        int d = off + i;
        int adx = x2 > x1 ? x2 - x1 : x1 - x2;
        int ady = y2 > y1 ? y2 - y1 : y1 - y2;
        if (adx >= ady) shrine_line(x1, y1 + d, x2, y2 + d, c);
        else            shrine_line(x1 + d, y1, x2 + d, y2, c);
    }
}

void sprite_render_stream(const uint8_t *stream, unsigned size,
                          int cx, int cy)
{
    if (!stream || size == 0) return;
    color_t cur = C_WHITE;
    int thick = 1;
    unsigned off = 0;
    int guard = 300;

    while (off < size && guard-- > 0) {
        uint8_t op = stream[off];
        switch (op) {
        case SPT_END: return;
        case SPT_COLOR:
            if (off + 1 >= size) return;
            cur = (color_t)(stream[off + 1] & 15);
            off += 2; break;
        case SPT_DITHER_COLOR:
            if (off + 2 >= size) return;
            cur = (color_t)(stream[off + 1] & 15);
            off += 3; break;
        case SPT_THICK:
            if (off + 4 >= size) return;
            thick = rd_i32(stream, off + 1);
            if (thick < 1) thick = 1;
            off += 5; break;
        case SPT_TRANSFORM_ON:
        case SPT_TRANSFORM_OFF:
            off += 1; break;
        case SPT_PT:
        case SPT_SHIFT: {
            if (off + 8 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            shrine_pixel(cx + x, cy + y, cur);
            off += 9; break;
        }
        case SPT_LINE:
        case SPT_ARROW: {
            if (off + 16 >= size) return;
            int x1 = rd_i32(stream, off + 1);
            int y1 = rd_i32(stream, off + 5);
            int x2 = rd_i32(stream, off + 9);
            int y2 = rd_i32(stream, off + 13);
            thick_line(cx + x1, cy + y1, cx + x2, cy + y2, cur, thick);
            off += 17; break;
        }
        case SPT_RECT: {
            if (off + 16 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int w = rd_i32(stream, off + 9);
            int h = rd_i32(stream, off + 13);
            shrine_fill_rect(cx + x, cy + y, w, h, cur);
            off += 17; break;
        }
        case SPT_CIRCLE: {
            if (off + 12 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int r = rd_i32(stream, off + 9);
            shrine_circle(cx + x, cy + y, r, cur);
            off += 13; break;
        }
        case SPT_FLOOD_FILL:
        case SPT_FLOOD_FILL_NOT:
            // Not supported in direct mode.
            off += 9; break;
        case SPT_BITMAP: {
            if (off + 16 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int w = rd_i32(stream, off + 9);
            int h = rd_i32(stream, off + 13);
            unsigned pix_off = off + 17;
            if (w <= 0 || h <= 0 || w > 200 || h > 200) return;
            if (pix_off + (unsigned)(w * h) > size) return;
            for (int r = 0; r < h; r++) {
                for (int c = 0; c < w; c++) {
                    uint8_t p = stream[pix_off + r * w + c];
                    if (p == 0xFF) continue;
                    if (p < 16) shrine_pixel(cx + x + c, cy + y + r, (color_t)p);
                }
            }
            off = pix_off + w * h;
            break;
        }
        default:
            return;
        }
    }
}

// ------------------------------------------------------------------
//  Buffered (in-memory) mode
// ------------------------------------------------------------------

// Sentinel for "not yet drawn" pixels — outside the RGB565 domain, using
// bit patterns that never appear in the 16-color palette we use for
// sprites (all our palette entries have specific bit layouts).
// Callers memset the buffer with this before rendering.
#define FB_EMPTY 0x0000

static inline void bfr_pixel(uint16_t *fb, int W, int H,
                             int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
        fb[y * W + x] = c;
}

static void bfr_line(uint16_t *fb, int W, int H,
                     int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        bfr_pixel(fb, W, H, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void bfr_thick_line(uint16_t *fb, int W, int H,
                           int x0, int y0, int x1, int y1,
                           uint16_t c, int thick)
{
    if (thick < 1) thick = 1;
    if (thick > 5) thick = 5;
    int off = -(thick / 2);
    for (int i = 0; i < thick; i++) {
        int d = off + i;
        int adx = x1 > x0 ? x1 - x0 : x0 - x1;
        int ady = y1 > y0 ? y1 - y0 : y0 - y1;
        if (adx >= ady) bfr_line(fb, W, H, x0, y0 + d, x1, y1 + d, c);
        else            bfr_line(fb, W, H, x0 + d, y0, x1 + d, y1, c);
    }
}

static void bfr_rect_filled(uint16_t *fb, int W, int H,
                            int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= H) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px < 0 || px >= W) continue;
            fb[py * W + px] = c;
        }
    }
}

static void bfr_circle(uint16_t *fb, int W, int H,
                       int cx, int cy, int r, uint16_t c)
{
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        bfr_pixel(fb, W, H, cx + x, cy + y, c); bfr_pixel(fb, W, H, cx - x, cy + y, c);
        bfr_pixel(fb, W, H, cx + x, cy - y, c); bfr_pixel(fb, W, H, cx - x, cy - y, c);
        bfr_pixel(fb, W, H, cx + y, cy + x, c); bfr_pixel(fb, W, H, cx - y, cy + x, c);
        bfr_pixel(fb, W, H, cx + y, cy - x, c); bfr_pixel(fb, W, H, cx - y, cy - x, c);
        x++;
        if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6;
    }
}

// Scanline flood fill. Static stack keeps recursion off the task stack.
#define FF_STACK 512
static int16_t s_ff_stack[FF_STACK][2];

static void bfr_flood_fill(uint16_t *fb, int W, int H,
                           int sx, int sy, uint16_t fill_color)
{
    if ((unsigned)sx >= (unsigned)W || (unsigned)sy >= (unsigned)H) return;
    uint16_t target = fb[sy * W + sx];
    if (target == fill_color) return;

    int sp = 0;
    s_ff_stack[sp][0] = (int16_t)sx;
    s_ff_stack[sp][1] = (int16_t)sy;
    sp++;

    while (sp > 0) {
        sp--;
        int x = s_ff_stack[sp][0];
        int y = s_ff_stack[sp][1];
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) continue;
        if (fb[y * W + x] != target) continue;

        // Extend the current scanline as far left and right as it matches.
        int xl = x;
        while (xl > 0     && fb[y * W + (xl - 1)] == target) xl--;
        int xr = x;
        while (xr < W - 1 && fb[y * W + (xr + 1)] == target) xr++;
        for (int i = xl; i <= xr; i++) fb[y * W + i] = fill_color;

        // Push rows above and below, one seed per contiguous target run.
        for (int scan = -1; scan <= 1; scan += 2) {
            int ny = y + scan;
            if (ny < 0 || ny >= H) continue;
            bool in_run = false;
            for (int i = xl; i <= xr; i++) {
                bool m = (fb[ny * W + i] == target);
                if (m && !in_run) {
                    if (sp < FF_STACK) {
                        s_ff_stack[sp][0] = (int16_t)i;
                        s_ff_stack[sp][1] = (int16_t)ny;
                        sp++;
                    }
                    in_run = true;
                } else if (!m) {
                    in_run = false;
                }
            }
        }
    }
}

void sprite_render_buffered(uint16_t *fb, int W, int H,
                            int cx, int cy,
                            const uint8_t *stream, unsigned size)
{
    if (!fb || !stream || size == 0) return;
    color_t cur = C_WHITE;
    int thick = 1;
    unsigned off = 0;
    int guard = 300;

    while (off < size && guard-- > 0) {
        uint8_t op = stream[off];
        switch (op) {
        case SPT_END: return;
        case SPT_COLOR:
            if (off + 1 >= size) return;
            cur = (color_t)(stream[off + 1] & 15);
            off += 2; break;
        case SPT_DITHER_COLOR:
            if (off + 2 >= size) return;
            cur = (color_t)(stream[off + 1] & 15);
            off += 3; break;
        case SPT_THICK:
            if (off + 4 >= size) return;
            thick = rd_i32(stream, off + 1);
            if (thick < 1) thick = 1;
            off += 5; break;
        case SPT_TRANSFORM_ON:
        case SPT_TRANSFORM_OFF:
            off += 1; break;
        case SPT_PT:
        case SPT_SHIFT: {
            if (off + 8 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            bfr_pixel(fb, W, H, cx + x, cy + y, PAL_RGB565[cur & 15]);
            off += 9; break;
        }
        case SPT_LINE:
        case SPT_ARROW: {
            if (off + 16 >= size) return;
            int x1 = rd_i32(stream, off + 1);
            int y1 = rd_i32(stream, off + 5);
            int x2 = rd_i32(stream, off + 9);
            int y2 = rd_i32(stream, off + 13);
            bfr_thick_line(fb, W, H, cx + x1, cy + y1, cx + x2, cy + y2,
                           PAL_RGB565[cur & 15], thick);
            off += 17; break;
        }
        case SPT_RECT: {
            if (off + 16 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int w = rd_i32(stream, off + 9);
            int h = rd_i32(stream, off + 13);
            bfr_rect_filled(fb, W, H, cx + x, cy + y, w, h,
                            PAL_RGB565[cur & 15]);
            off += 17; break;
        }
        case SPT_CIRCLE: {
            if (off + 12 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int r = rd_i32(stream, off + 9);
            bfr_circle(fb, W, H, cx + x, cy + y, r, PAL_RGB565[cur & 15]);
            off += 13; break;
        }
        case SPT_FLOOD_FILL:
        case SPT_FLOOD_FILL_NOT: {
            if (off + 8 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            bfr_flood_fill(fb, W, H, cx + x, cy + y, PAL_RGB565[cur & 15]);
            off += 9; break;
        }
        case SPT_BITMAP: {
            if (off + 16 >= size) return;
            int x = rd_i32(stream, off + 1);
            int y = rd_i32(stream, off + 5);
            int w = rd_i32(stream, off + 9);
            int h = rd_i32(stream, off + 13);
            unsigned pix_off = off + 17;
            if (w <= 0 || h <= 0 || w > 200 || h > 200) return;
            if (pix_off + (unsigned)(w * h) > size) return;
            for (int r = 0; r < h; r++) {
                for (int c = 0; c < w; c++) {
                    uint8_t p = stream[pix_off + r * w + c];
                    if (p == 0xFF) continue;
                    if (p < 16)
                        bfr_pixel(fb, W, H, cx + x + c, cy + y + r,
                                  PAL_RGB565[p]);
                }
            }
            off = pix_off + w * h;
            break;
        }
        default:
            return;
        }
    }
}

// Convenience: render into a scratch buffer of the caller's size, then
// blit to the panel at (dst_x, dst_y). Anchor (cx, cy) is relative to
// the buffer's top-left.
//
// Includes Terry's flood-fill leak check: if a SPT_FLOOD_FILL op
// escapes the outline it was meant to color (because the outline had a
// gap), the corner pixel gets overwritten. In that case we undo by
// re-flooding from the corner back to bg_fill. This is exactly what
// Terry does in FlapBat's DrawIt with limit_flood_fill_dc.
#include "display.h"
void sprite_render_to_panel(int dst_x, int dst_y,
                            int buf_w, int buf_h,
                            int cx, int cy,
                            const uint8_t *stream, unsigned size,
                            uint16_t bg_fill)
{
    if (buf_w <= 0 || buf_h <= 0 || buf_w > 64 || buf_h > 64) return;
    static uint16_t scratch[64 * 64];
    int px = buf_w * buf_h;
    for (int i = 0; i < px; i++) scratch[i] = bg_fill;
    sprite_render_buffered(scratch, buf_w, buf_h, cx, cy, stream, size);
    // Leak-check + undo. Cheap when no leak (single comparison).
    if (scratch[0] != bg_fill) {
        bfr_flood_fill(scratch, buf_w, buf_h, 0, 0, bg_fill);
    }
    display_blit(dst_x, dst_y, buf_w, buf_h, scratch);
}

// ------------------------------------------------------------------
//  Legacy stubs
// ------------------------------------------------------------------

void sprite_render(const sprite_ref_t *sp, int x, int y)
{
    if (sp) sprite_render_stream(sp->data, sp->size, x, y);
}

int sprite_scan_tail(const uint8_t *tail, uint32_t tail_size,
                     sprite_ref_t *out, int max_out)
{
    (void)tail; (void)tail_size; (void)out; (void)max_out;
    return 0;
}
