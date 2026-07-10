// shrine.c — implementation of the TempleOS-flavored shim.
// Everything sits on top of display.c / input.c / audio.c.

#include "shrine.h"
#include "hw.h"
#include "display.h"
#include "input.h"
#include "audio.h"
#include "font8x8.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_random.h"

static inline uint16_t rgb(color_t c) { return PAL_RGB565[c & 15]; }

void shrine_init(void)
{
    display_init();
    input_init();
    audio_init();
}

void shrine_frame_begin(void) {}
void shrine_frame_end(void)   {}

// --- Primitives ---

void shrine_clear(color_t c) { display_fill_screen(rgb(c)); }
void shrine_pixel(int x, int y, color_t c) { display_pixel(x, y, rgb(c)); }
void shrine_hline(int x, int y, int w, color_t c) { display_hline(x, y, w, rgb(c)); }
void shrine_vline(int x, int y, int h, color_t c) { display_vline(x, y, h, rgb(c)); }
void shrine_rect(int x, int y, int w, int h, color_t c) { display_rect(x, y, w, h, rgb(c)); }
void shrine_fill_rect(int x, int y, int w, int h, color_t c) { display_fill_rect(x, y, w, h, rgb(c)); }

void shrine_line(int x0, int y0, int x1, int y1, color_t c)
{
    // Bresenham.
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        display_pixel(x0, y0, rgb(c));
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void shrine_circle(int cx, int cy, int r, color_t c)
{
    int x = 0, y = r, d = 3 - 2 * r;
    uint16_t col = rgb(c);
    while (y >= x) {
        display_pixel(cx + x, cy + y, col); display_pixel(cx - x, cy + y, col);
        display_pixel(cx + x, cy - y, col); display_pixel(cx - x, cy - y, col);
        display_pixel(cx + y, cy + x, col); display_pixel(cx - y, cy + x, col);
        display_pixel(cx + y, cy - x, col); display_pixel(cx - y, cy - x, col);
        x++;
        if (d > 0) { y--; d = d + 4 * (x - y) + 10; }
        else       {      d = d + 4 * x + 6; }
    }
}

void shrine_fill_circle(int cx, int cy, int r, color_t c)
{
    int r2 = r * r;
    uint16_t col = rgb(c);
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        display_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, col);
    }
}

// --- Sprites ---

void shrine_sprite8(int x, int y, const uint8_t *glyph,
                    color_t fg, color_t bg)
{
    uint16_t f = rgb(fg), b = rgb(bg);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            display_pixel(x + col, y + row, (bits & (1 << col)) ? f : b);
        }
    }
}

void shrine_sprite8_masked(int x, int y, const uint8_t *glyph, color_t fg)
{
    uint16_t f = rgb(fg);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) display_pixel(x + col, y + row, f);
        }
    }
}

// --- Text ---

void shrine_putcxy(int x, int y, char c, color_t fg, color_t bg)
{
    uint8_t ch = (uint8_t)c;
    if (ch >= 128) ch = ' ';
    shrine_sprite8(x, y, FONT8X8[ch], fg, bg);
}

void shrine_putsxy(int x, int y, const char *s, color_t fg, color_t bg)
{
    while (*s) {
        shrine_putcxy(x, y, *s++, fg, bg);
        x += GLYPH_W;
    }
}

void shrine_putc(int col, int row, char c, color_t fg, color_t bg)
{
    shrine_putcxy(col * GLYPH_W, row * GLYPH_H, c, fg, bg);
}

void shrine_puts(int col, int row, const char *s, color_t fg, color_t bg)
{
    shrine_putsxy(col * GLYPH_W, row * GLYPH_H, s, fg, bg);
}

int shrine_text_width_px(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n * GLYPH_W;
}

void shrine_puts_centered(int row, const char *s, color_t fg, color_t bg)
{
    int w = shrine_text_width_px(s);
    int x = (SCREEN_W - w) / 2;
    shrine_putsxy(x, row * GLYPH_H, s, fg, bg);
}

void shrine_putsxy_scaled(int x, int y, const char *s, color_t fg, color_t bg,
                          int scale)
{
    if (scale < 1) scale = 1;
    uint16_t f = PAL_RGB565[fg & 15], b = PAL_RGB565[bg & 15];
    while (*s) {
        uint8_t ch = (uint8_t)*s++;
        if (ch >= 128) ch = ' ';
        const uint8_t *g = FONT8X8[ch];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++) {
                uint16_t c = (bits & (1 << col)) ? f : b;
                display_fill_rect(x + col * scale, y + row * scale,
                                  scale, scale, c);
            }
        }
        x += 8 * scale;
    }
}

void shrine_puts_centered_scaled(int y, const char *s, color_t fg, color_t bg,
                                 int scale)
{
    int w = 8 * scale * (int)strlen(s);
    int x = (SCREEN_W - w) / 2;
    shrine_putsxy_scaled(x, y, s, fg, bg, scale);
}

// --- Windows ---

void shrine_window(int col, int row, int cols, int rows,
                   color_t border, color_t fill)
{
    // Fill interior.
    shrine_fill_rect(col * GLYPH_W, row * GLYPH_H,
                     cols * GLYPH_W, rows * GLYPH_H, fill);
    // Corners.
    shrine_putc(col,             row,              G_ULCORN[0], border, fill);
    shrine_putc(col + cols - 1,  row,              G_URCORN[0], border, fill);
    shrine_putc(col,             row + rows - 1,   G_LLCORN[0], border, fill);
    shrine_putc(col + cols - 1,  row + rows - 1,   G_LRCORN[0], border, fill);
    // Top/bottom edges.
    for (int i = 1; i < cols - 1; i++) {
        shrine_putc(col + i, row,             G_HLINE[0], border, fill);
        shrine_putc(col + i, row + rows - 1,  G_HLINE[0], border, fill);
    }
    // Left/right edges.
    for (int i = 1; i < rows - 1; i++) {
        shrine_putc(col,            row + i, G_VLINE[0], border, fill);
        shrine_putc(col + cols - 1, row + i, G_VLINE[0], border, fill);
    }
}

// --- Audio pass-through ---

void shrine_beep(int hz, int ms) { audio_beep(hz, ms); }
void shrine_snd_off(void)         { audio_off(); }

// --- Timing ---

uint32_t shrine_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void shrine_sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// --- Input ---

void shrine_input_scan(void)         { input_scan(); }
bool shrine_key_pressed(btn_t b)     { return input_pressed(b); }
bool shrine_key_held(btn_t b)        { return input_held(b); }
bool shrine_any_pressed(void)        { return input_any_pressed(); }

bool shrine_should_quit(void)
{
    return input_hold_ms(BTN_BOOT) >= QUIT_HOLD_MS;
}

// --- RNG ---

uint32_t shrine_god(uint32_t upper_exclusive)
{
    if (upper_exclusive == 0) return 0;
    return esp_random() % upper_exclusive;
}
