// templeshim.c — implementation of the TempleOS-compatible Gr* API.
//
// Scene code writes coordinates in Terry's 640x480 space; every
// primitive divides by dc->scale on entry so we can render into the
// 320x240 panel unchanged. Colors are TempleOS palette indices;
// PAL_RGB565 converts on write.

#include "templeshim.h"
#include "display.h"
#include "font8x8.h"
#include "sprite_decoder.h"
#include "palette.h"

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

CDC gr_dc;

void CDCInit(uint16_t *fb, int fb_w, int fb_h, int scale)
{
    gr_dc.fb    = fb;
    gr_dc.fb_w  = fb_w;
    gr_dc.fb_h  = fb_h;
    gr_dc.scale = scale < 1 ? 1 : scale;
    gr_dc.color = C_WHITE;
    gr_dc.thick = 1;
    gr_dc.flags = 0;
    Mat4x4IdentEqu(gr_dc.r);
}

void DCPresent(CDC *dc)
{
    if (dc && dc->fb) display_present_full(dc->fb);
}

void DCFill(CDC *dc)
{
    if (!dc || !dc->fb) return;
    uint16_t v = PAL_RGB565[dc->color & 15];
    int n = dc->fb_w * dc->fb_h;
    for (int i = 0; i < n; i++) dc->fb[i] = v;
}

// --- Matrix ops (row-major 4x4) ---

static void mat4_mul(float dst[16], const float a[16], const float b[16])
{
    float t[16];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
            t[r * 4 + c] = s;
        }
    }
    memcpy(dst, t, sizeof(t));
}

void Mat4x4IdentEqu(float m[16])
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void Mat4x4RotX(float m[16], float ang)
{
    float c = cosf(ang), s = sinf(ang);
    float r[16] = { 1,0,0,0, 0,c,-s,0, 0,s,c,0, 0,0,0,1 };
    mat4_mul(m, r, m);
}

void Mat4x4RotY(float m[16], float ang)
{
    float c = cosf(ang), s = sinf(ang);
    float r[16] = { c,0,s,0, 0,1,0,0, -s,0,c,0, 0,0,0,1 };
    mat4_mul(m, r, m);
}

void Mat4x4RotZ(float m[16], float ang)
{
    float c = cosf(ang), s = sinf(ang);
    float r[16] = { c,-s,0,0, s,c,0,0, 0,0,1,0, 0,0,0,1 };
    mat4_mul(m, r, m);
}

void Mat4x4Scale(float m[16], float sc)
{
    float r[16] = { sc,0,0,0, 0,sc,0,0, 0,0,sc,0, 0,0,0,1 };
    mat4_mul(m, r, m);
}

void Mat4x4Translate(float m[16], float x, float y, float z)
{
    float r[16] = { 1,0,0,x, 0,1,0,y, 0,0,1,z, 0,0,0,1 };
    mat4_mul(m, r, m);
}

void Mat4x4MulXYZ(const float m[16], float *x, float *y, float *z)
{
    float px = *x, py = *y, pz = *z;
    *x = m[0] * px + m[1] * py + m[2]  * pz + m[3];
    *y = m[4] * px + m[5] * py + m[6]  * pz + m[7];
    *z = m[8] * px + m[9] * py + m[10] * pz + m[11];
}

// --- Screen mapping ---

static inline int sx(const CDC *dc, int x) { return x / dc->scale; }
static inline int sy(const CDC *dc, int y) { return y / dc->scale; }

static inline void put_px(CDC *dc, int x, int y, color_t c)
{
    if ((unsigned)x < (unsigned)dc->fb_w && (unsigned)y < (unsigned)dc->fb_h) {
        dc->fb[y * dc->fb_w + x] = PAL_RGB565[c & 15];
    }
}

// Apply DCF_TRANSFORMATION if set. Coordinates are already in Terry space
// on entry — matrix transforms leave them in Terry space too, and we do
// the scale down at the very end (put_px goes through sx/sy already).
static void apply_transform(const CDC *dc, float *x, float *y, float *z)
{
    if (dc->flags & DCF_TRANSFORMATION) {
        Mat4x4MulXYZ(dc->r, x, y, z);
    }
}

// --- Primitives (Terry-coord in, framebuffer out) ---

void GrPlot(CDC *dc, int x, int y)
{
    put_px(dc, sx(dc, x), sy(dc, y), dc->color);
}

void GrPlot3(CDC *dc, int x, int y, int z)
{
    float fx = x, fy = y, fz = z;
    apply_transform(dc, &fx, &fy, &fz);
    put_px(dc, sx(dc, (int)fx), sy(dc, (int)fy), dc->color);
}

// Bresenham line in framebuffer space.
static void raw_line(CDC *dc, int x0, int y0, int x1, int y1, color_t c)
{
    int dx = abs(x1 - x0), sx1 = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy1 = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        put_px(dc, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx1; }
        if (e2 <= dx) { err += dx; y0 += sy1; }
    }
}

static void draw_thick_line(CDC *dc, int x0, int y0, int x1, int y1)
{
    int t = dc->thick;
    if (t < 1) t = 1;
    if (t > 5) t = 5;
    int off = -(t / 2);
    int adx = x1 > x0 ? x1 - x0 : x0 - x1;
    int ady = y1 > y0 ? y1 - y0 : y0 - y1;
    for (int i = 0; i < t; i++) {
        int d = off + i;
        if (adx >= ady) raw_line(dc, x0, y0 + d, x1, y1 + d, dc->color);
        else            raw_line(dc, x0 + d, y0, x1 + d, y1, dc->color);
    }
}

void GrLine(CDC *dc, int x1, int y1, int x2, int y2)
{
    draw_thick_line(dc, sx(dc, x1), sy(dc, y1), sx(dc, x2), sy(dc, y2));
}

void GrLine3(CDC *dc, int x1, int y1, int z1, int x2, int y2, int z2)
{
    float ax = x1, ay = y1, az = z1;
    float bx = x2, by = y2, bz = z2;
    apply_transform(dc, &ax, &ay, &az);
    apply_transform(dc, &bx, &by, &bz);
    draw_thick_line(dc, sx(dc, (int)ax), sy(dc, (int)ay),
                        sx(dc, (int)bx), sy(dc, (int)by));
}

void GrFillRect(CDC *dc, int x, int y, int w, int h)
{
    int fx = sx(dc, x), fy = sy(dc, y);
    int fw = sx(dc, x + w) - fx;
    int fh = sy(dc, y + h) - fy;
    if (fw <= 0 || fh <= 0) return;
    if (fx < 0) { fw += fx; fx = 0; }
    if (fy < 0) { fh += fy; fy = 0; }
    if (fx + fw > dc->fb_w) fw = dc->fb_w - fx;
    if (fy + fh > dc->fb_h) fh = dc->fb_h - fy;
    if (fw <= 0 || fh <= 0) return;
    uint16_t v = PAL_RGB565[dc->color & 15];
    for (int j = 0; j < fh; j++) {
        uint16_t *p = &dc->fb[(fy + j) * dc->fb_w + fx];
        for (int i = 0; i < fw; i++) p[i] = v;
    }
}

void GrRect(CDC *dc, int x, int y, int w, int h)
{
    int fx = sx(dc, x), fy = sy(dc, y);
    int fw = sx(dc, x + w) - fx;
    int fh = sy(dc, y + h) - fy;
    raw_line(dc, fx,          fy,          fx + fw - 1, fy,          dc->color);
    raw_line(dc, fx,          fy + fh - 1, fx + fw - 1, fy + fh - 1, dc->color);
    raw_line(dc, fx,          fy,          fx,          fy + fh - 1, dc->color);
    raw_line(dc, fx + fw - 1, fy,          fx + fw - 1, fy + fh - 1, dc->color);
}

void GrCircle(CDC *dc, int cx, int cy, int r)
{
    int fcx = sx(dc, cx), fcy = sy(dc, cy), fr = r / dc->scale;
    int x = 0, y = fr, d = 3 - 2 * fr;
    while (y >= x) {
        put_px(dc, fcx + x, fcy + y, dc->color); put_px(dc, fcx - x, fcy + y, dc->color);
        put_px(dc, fcx + x, fcy - y, dc->color); put_px(dc, fcx - x, fcy - y, dc->color);
        put_px(dc, fcx + y, fcy + x, dc->color); put_px(dc, fcx - y, fcy + x, dc->color);
        put_px(dc, fcx + y, fcy - x, dc->color); put_px(dc, fcx - y, fcy - x, dc->color);
        x++;
        if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6;
    }
}

void GrFillCircle(CDC *dc, int cx, int cy, int r)
{
    int fcx = sx(dc, cx), fcy = sy(dc, cy), fr = r / dc->scale;
    int r2 = fr * fr;
    uint16_t v = PAL_RGB565[dc->color & 15];
    for (int dy = -fr; dy <= fr; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        int py = fcy + dy;
        if ((unsigned)py >= (unsigned)dc->fb_h) continue;
        int x0 = fcx - dx, x1 = fcx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= dc->fb_w) x1 = dc->fb_w - 1;
        uint16_t *p = &dc->fb[py * dc->fb_w + x0];
        for (int i = 0; i <= x1 - x0; i++) p[i] = v;
    }
}

// Scanline flood fill; small static stack keeps recursion off the task
// stack. Same algorithm as sprite_decoder's bfr_flood_fill.
#define GR_FF_STACK 512
static int16_t s_gr_ff[GR_FF_STACK][2];

void GrFloodFill(CDC *dc, int x, int y)
{
    int fx = sx(dc, x), fy = sy(dc, y);
    if ((unsigned)fx >= (unsigned)dc->fb_w
        || (unsigned)fy >= (unsigned)dc->fb_h) return;
    uint16_t target = dc->fb[fy * dc->fb_w + fx];
    uint16_t fill   = PAL_RGB565[dc->color & 15];
    if (target == fill) return;

    int sp = 0;
    s_gr_ff[sp][0] = fx; s_gr_ff[sp][1] = fy; sp++;
    while (sp > 0) {
        sp--;
        int px = s_gr_ff[sp][0];
        int py = s_gr_ff[sp][1];
        if ((unsigned)px >= (unsigned)dc->fb_w
            || (unsigned)py >= (unsigned)dc->fb_h) continue;
        if (dc->fb[py * dc->fb_w + px] != target) continue;

        int xl = px;
        while (xl > 0 && dc->fb[py * dc->fb_w + (xl - 1)] == target) xl--;
        int xr = px;
        while (xr < dc->fb_w - 1
               && dc->fb[py * dc->fb_w + (xr + 1)] == target) xr++;
        for (int i = xl; i <= xr; i++) dc->fb[py * dc->fb_w + i] = fill;
        for (int scan = -1; scan <= 1; scan += 2) {
            int ny = py + scan;
            if (ny < 0 || ny >= dc->fb_h) continue;
            bool in_run = false;
            for (int i = xl; i <= xr; i++) {
                bool m = (dc->fb[ny * dc->fb_w + i] == target);
                if (m && !in_run) {
                    if (sp < GR_FF_STACK) {
                        s_gr_ff[sp][0] = i; s_gr_ff[sp][1] = ny; sp++;
                    }
                    in_run = true;
                } else if (!m) in_run = false;
            }
        }
    }
}

// GrPrint uses our 8x8 font in "framebuffer pixels" at the given point.
void GrPrint(CDC *dc, int x, int y, const char *s)
{
    int px = sx(dc, x), py = sy(dc, y);
    color_t fg = dc->color;
    while (*s) {
        uint8_t c = (uint8_t)*s++;
        if (c >= 128) c = ' ';
        const uint8_t *g = FONT8X8[c];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << col)) put_px(dc, px + col, py + row, fg);
            }
        }
        px += 8;
    }
}

color_t GrPeek(CDC *dc, int x, int y)
{
    int fx = sx(dc, x), fy = sy(dc, y);
    if ((unsigned)fx >= (unsigned)dc->fb_w
        || (unsigned)fy >= (unsigned)dc->fb_h) return C_BLACK;
    uint16_t pix = dc->fb[fy * dc->fb_w + fx];
    for (int i = 0; i < 16; i++) if (PAL_RGB565[i] == pix) return (color_t)i;
    return C_BLACK;
}

// --- Sprite3: walk a CSprite command stream, apply DC state to each op ---

static inline int32_t rd_i32(const uint8_t *b, int o)
{
    return (int32_t)((uint32_t)b[o]
                   | ((uint32_t)b[o+1] <<  8)
                   | ((uint32_t)b[o+2] << 16)
                   | ((uint32_t)b[o+3] << 24));
}

void Sprite3(CDC *dc, int x, int y, int z,
             const uint8_t *stream, unsigned size)
{
    if (!dc || !stream || size == 0) return;
    unsigned off = 0;
    int guard = 400;
    while (off < size && guard-- > 0) {
        uint8_t op = stream[off];
        switch (op) {
        case SPT_END: return;
        case SPT_COLOR:
            if (off + 1 >= size) return;
            dc->color = (color_t)(stream[off + 1] & 15);
            off += 2; break;
        case SPT_DITHER_COLOR:
            if (off + 2 >= size) return;
            dc->color = (color_t)(stream[off + 1] & 15);
            off += 3; break;
        case SPT_THICK:
            if (off + 4 >= size) return;
            dc->thick = rd_i32(stream, off + 1);
            if (dc->thick < 1) dc->thick = 1;
            off += 5; break;
        case SPT_TRANSFORM_ON:  dc->flags |=  DCF_TRANSFORMATION; off += 1; break;
        case SPT_TRANSFORM_OFF: dc->flags &= ~DCF_TRANSFORMATION; off += 1; break;
        case SPT_PT:
        case SPT_SHIFT: {
            if (off + 8 >= size) return;
            int px = rd_i32(stream, off + 1) + x;
            int py = rd_i32(stream, off + 5) + y;
            GrPlot3(dc, px, py, z);
            off += 9; break;
        }
        case SPT_LINE:
        case SPT_ARROW: {
            if (off + 16 >= size) return;
            int ax = rd_i32(stream, off +  1) + x;
            int ay = rd_i32(stream, off +  5) + y;
            int bx = rd_i32(stream, off +  9) + x;
            int by = rd_i32(stream, off + 13) + y;
            GrLine3(dc, ax, ay, z, bx, by, z);
            off += 17; break;
        }
        case SPT_RECT: {
            if (off + 16 >= size) return;
            int rx = rd_i32(stream, off +  1) + x;
            int ry = rd_i32(stream, off +  5) + y;
            int rw = rd_i32(stream, off +  9);
            int rh = rd_i32(stream, off + 13);
            GrFillRect(dc, rx, ry, rw, rh);
            off += 17; break;
        }
        case SPT_CIRCLE: {
            if (off + 12 >= size) return;
            int cx = rd_i32(stream, off + 1) + x;
            int cy = rd_i32(stream, off + 5) + y;
            int cr = rd_i32(stream, off + 9);
            GrCircle(dc, cx, cy, cr);
            off += 13; break;
        }
        case SPT_FLOOD_FILL:
        case SPT_FLOOD_FILL_NOT: {
            if (off + 8 >= size) return;
            int fx = rd_i32(stream, off + 1) + x;
            int fy = rd_i32(stream, off + 5) + y;
            GrFloodFill(dc, fx, fy);
            off += 9; break;
        }
        case SPT_BITMAP: {
            if (off + 16 >= size) return;
            int bx = rd_i32(stream, off +  1) + x;
            int by = rd_i32(stream, off +  5) + y;
            int bw = rd_i32(stream, off +  9);
            int bh = rd_i32(stream, off + 13);
            unsigned pix_off = off + 17;
            if (bw <= 0 || bh <= 0 || bw > 300 || bh > 300) return;
            if (pix_off + (unsigned)(bw * bh) > size) return;
            for (int r = 0; r < bh; r++) {
                for (int c = 0; c < bw; c++) {
                    uint8_t p = stream[pix_off + r * bw + c];
                    if (p == 0xFF) continue;
                    if (p < 16) {
                        int spx = sx(dc, bx + c);
                        int spy = sy(dc, by + r);
                        put_px(dc, spx, spy, (color_t)p);
                    }
                }
            }
            off = pix_off + bw * bh;
            break;
        }
        default:
            return;
        }
    }
}
