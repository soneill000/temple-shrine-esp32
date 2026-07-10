// shrine.h — the TempleOS-flavored graphics/input shim.
//
// Everything a ported "game" uses to draw and read input goes through here.
// Colors are always 16-color palette indices (color_t). Text is 8x8. There
// is no direct RGB565 in game code.
//
// This lets us swap the underlying display driver — e.g. add an SDL2
// backend for desktop development — without touching game code.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "palette.h"
#include "input.h"

// --- One-time init (calls display_init etc.) ---
void shrine_init(void);

// --- Frame flow ---
// The shrine does not double-buffer; drawing goes straight to the panel.
// These are here as no-ops today but let games express intent portably.
void shrine_frame_begin(void);
void shrine_frame_end(void);

// --- Pixel-level drawing ---
void shrine_clear(color_t c);
void shrine_pixel(int x, int y, color_t c);
void shrine_line(int x0, int y0, int x1, int y1, color_t c);
void shrine_hline(int x, int y, int w, color_t c);
void shrine_vline(int x, int y, int h, color_t c);
void shrine_rect(int x, int y, int w, int h, color_t c);
void shrine_fill_rect(int x, int y, int w, int h, color_t c);
void shrine_circle(int cx, int cy, int r, color_t c);
void shrine_fill_circle(int cx, int cy, int r, color_t c);

// --- Text (8x8 grid) ---
// col/row are in character cells (40 cols × 30 rows).
// x/y variants place at pixel coordinates.
void shrine_putc(int col, int row, char c, color_t fg, color_t bg);
void shrine_puts(int col, int row, const char *s, color_t fg, color_t bg);
void shrine_putcxy(int x, int y, char c, color_t fg, color_t bg);
void shrine_putsxy(int x, int y, const char *s, color_t fg, color_t bg);
void shrine_puts_centered(int row, const char *s, color_t fg, color_t bg);
int  shrine_text_width_px(const char *s);   // n * 8

// Scaled text — each source pixel becomes a scale×scale block.
void shrine_putsxy_scaled(int x, int y, const char *s, color_t fg, color_t bg,
                          int scale);
void shrine_puts_centered_scaled(int y, const char *s, color_t fg, color_t bg,
                                 int scale);

// --- 8x8 sprite from a 64-byte bitmap ---
// bits are packed the same as font glyphs (byte 0 = top row, bit 0 = left).
void shrine_sprite8(int x, int y, const uint8_t *glyph,
                    color_t fg, color_t bg);
// Same but transparent (bg pixels are not drawn).
void shrine_sprite8_masked(int x, int y, const uint8_t *glyph, color_t fg);

// --- TempleOS-style bordered "window" ---
// Draws box-drawing chars around a text region.
void shrine_window(int col, int row, int cols, int rows,
                   color_t border, color_t fill);

// --- Audio pass-through (TempleOS Beep / Snd) ---
// Games use these instead of the audio.h API directly so future backends
// can redirect.
void shrine_beep(int hz, int ms);
void shrine_snd_off(void);

// --- Timing ---
uint32_t shrine_ms(void);
void     shrine_sleep_ms(uint32_t ms);

// --- Input (thin wrapper over input.h with TempleOS scan-code feel) ---
// Called by main loop before each game tick.
void shrine_input_scan(void);
bool shrine_key_pressed(btn_t b);
bool shrine_key_held(btn_t b);
bool shrine_any_pressed(void);

// --- Quit gesture ---
// A game returns to the launcher when the user holds BOOT for QUIT_HOLD_MS.
// Games poll this in their main loop.
#define QUIT_HOLD_MS   700
bool shrine_should_quit(void);

// --- RNG (Terry's "God, the RNG") ---
uint32_t shrine_god(uint32_t upper_exclusive);
