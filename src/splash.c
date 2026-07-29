// splash.c — TempleOS-style Welcome.DD splash.
// Deep blue background, yellow title, cyan sub-heading, blinking prompt,
// a random GodWord "God says: X" line, and a small bobbing bat sprite.
//
// Redraw discipline: paint everything ONCE at start, then only touch the
// dirty regions (bats, prompt, word) each frame. A full clear+redraw per
// 250 ms took long enough that you could watch the repaint and input
// scans were starving.

#include "splash.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "vocab.h"

#include <stdio.h>

#define BAT_L_X    (6  * GLYPH_W)
#define BAT_R_X    (SCREEN_W - 7 * GLYPH_W)
#define BAT_BASE_Y (19 * GLYPH_H)
#define BAT_BOX_H  10

// Draw a thick straight line by walking a Bresenham-ish parametric
// path and stamping a `thick`x`thick` square at each step. shrine_line
// only draws single-pixel; we use this for the sword bevel.
static void thick_line(int x0, int y0, int x1, int y1, int thick, color_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
              ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps <= 0) return;
    int half = thick / 2;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps - half;
        int y = y0 + dy * i / steps - half;
        shrine_fill_rect(x, y, thick, thick, c);
    }
}

// Terry's After Egypt tile icon — scale + sword in a blue-framed cyan
// panel — reproduced procedurally at fb pixel scale. Centered at
// (cx, cy), spans 2*half pixels each side.
static void draw_ae_emblem(int cx, int cy, int half)
{
    int x0 = cx - half, y0 = cy - half;
    int size = half * 2;
    int b    = size / 10; if (b < 2) b = 2;

    // Blue frame + cyan interior.
    shrine_fill_rect(x0, y0, size, size, C_BLUE);
    shrine_fill_rect(x0 + b, y0 + b, size - 2 * b, size - 2 * b, C_LTCYAN);

    // Sword — diagonal from lower-left handle to upper-right tip,
    // layered thick lines for silver-bevel look.
    int hx = cx - half * 3 / 8;
    int hy = cy + half * 3 / 4;
    int tx = cx + half * 3 / 4;
    int ty = cy - half * 3 / 4;
    thick_line(hx, hy, tx, ty, 7, C_BLACK);
    thick_line(hx, hy, tx, ty, 5, C_WHITE);
    thick_line(hx, hy, tx, ty, 3, C_LTGRAY);
    thick_line(hx, hy, tx, ty, 1, C_WHITE);

    // Yellow crossguard just above the handle (axis-aligned slab).
    int gx = hx + (tx - hx) / 8;
    int gy = hy + (ty - hy) / 8;
    int gw = size / 10 + 2;
    int gh = size / 24 + 2;
    if (gw < 6) gw = 6;
    if (gh < 3) gh = 3;
    shrine_fill_rect(gx - gw / 2 - 1, gy - gh / 2 - 1, gw + 2, gh + 2, C_BLACK);
    shrine_fill_rect(gx - gw / 2,     gy - gh / 2,     gw,     gh,     C_YELLOW);

    // Scale wedge — filled yellow triangle at top center.
    int apex_x = cx;
    int apex_y = cy - half * 3 / 4;
    int base_y = cy - half * 1 / 3;
    int half_base = half / 2;
    int slices = base_y - apex_y;
    for (int i = 0; i < slices; i++) {
        int hw = half_base * i / slices;
        shrine_fill_rect(apex_x - hw, apex_y + i, hw * 2, 1, C_YELLOW);
    }
    shrine_line(apex_x, apex_y, apex_x - half_base, base_y, C_BLACK);
    shrine_line(apex_x, apex_y, apex_x + half_base, base_y, C_BLACK);
    shrine_hline(apex_x - half_base, base_y, half_base * 2, C_BLACK);

    // Beam under the wedge.
    int beam_x1 = cx - half_base - 4;
    int beam_x2 = cx + half_base + 4;
    int beam_y  = base_y + 2;
    shrine_hline(beam_x1, beam_y,     beam_x2 - beam_x1, C_BLACK);
    shrine_hline(beam_x1, beam_y + 1, beam_x2 - beam_x1, C_BLACK);

    // Two hanging pans (cone outlines + yellow bottom).
    int pan_off_x  = half_base - 3;
    int pan_bot_y  = beam_y + half / 3;
    int pan_hw     = size / 14;
    if (pan_hw < 4) pan_hw = 4;
    for (int side = -1; side <= 1; side += 2) {
        int px = cx + side * pan_off_x;
        shrine_line(px, beam_y, px - pan_hw, pan_bot_y, C_BLACK);
        shrine_line(px, beam_y, px + pan_hw, pan_bot_y, C_BLACK);
        shrine_hline(px - pan_hw, pan_bot_y, pan_hw * 2, C_BLACK);
        int band_h = pan_hw > 6 ? 3 : 2;
        for (int dy = 0; dy < band_h; dy++) {
            int hw = pan_hw - dy - 1;
            if (hw < 1) continue;
            shrine_fill_rect(px - hw, pan_bot_y - band_h + dy,
                             hw * 2, 1, C_YELLOW);
        }
    }
}

// Main splash emblem — Terry's After Egypt scale+sword tile, sized to
// fit the splash's mid-band without clipping any surrounding text.
// Center y=125, half=30 -> tile spans fb y=95..155 (rows 12-19). No
// caption underneath since we don't want to imply this is a TempleOS
// port; the emblem stands alone.
static void draw_temple_logo(void)
{
    draw_ae_emblem(SCREEN_W / 2, 125, 30);
}

// Text rows sit either above the logo (rows 8-10) or below it
// (rows 21+). The logo tile spans roughly rows 12-19 so the middle
// band is reserved for it — see draw_temple_logo() for the exact
// centered position/size.
#define SPARKLE_ROW 21
#define WORD_ROW    24
#define PROMPT_ROW  26

// Decorative sparkling divider under the emblem. Draws a thin yellow
// horizontal line, then scatters ~6 small pixel sparkles at random x
// positions in bright colors. Called on a 250 ms cadence so the
// sparkles twinkle.
static void draw_sparkle_divider(int phase)
{
    int y0 = SPARKLE_ROW * GLYPH_H;      // top of the divider row
    int y_line = y0 + 3;                 // horizontal line row
    int width = SCREEN_W - 4 * GLYPH_W;  // leave margins matching window frame

    // Erase the row.
    shrine_fill_rect(2 * GLYPH_W, y0, width, GLYPH_H, PAL_RGB565[C_BG]);
    // Base line — dim yellow.
    shrine_fill_rect(2 * GLYPH_W, y_line, width, 1, PAL_RGB565[C_YELLOW]);

    // Little diamond decoration at each end of the line.
    int lx = 2 * GLYPH_W;
    int rx = SCREEN_W - 2 * GLYPH_W - 1;
    // Small filled diamond via 3-row stack: 1, 3, 1 pixels wide.
    shrine_fill_rect(lx,     y_line - 1, 3, 1, PAL_RGB565[C_YELLOW]);
    shrine_fill_rect(lx,     y_line,     3, 1, PAL_RGB565[C_YELLOW]);
    shrine_fill_rect(lx,     y_line + 1, 3, 1, PAL_RGB565[C_YELLOW]);
    shrine_fill_rect(rx - 2, y_line - 1, 3, 1, PAL_RGB565[C_YELLOW]);
    shrine_fill_rect(rx - 2, y_line,     3, 1, PAL_RGB565[C_YELLOW]);
    shrine_fill_rect(rx - 2, y_line + 1, 3, 1, PAL_RGB565[C_YELLOW]);

    // Sparkles — 6 twinkling pixels along the line. Position/color
    // hashed off `phase` so they shift each redraw and read as motion.
    static const color_t COLORS[4] = { C_WHITE, C_LTCYAN, C_YELLOW, C_LTMAGENTA };
    int inner_x = 3 * GLYPH_W;
    int inner_w = width - 2 * GLYPH_W;
    for (int i = 0; i < 6; i++) {
        // Cheap deterministic hash for placement.
        uint32_t h = (uint32_t)(phase * 2654435761u) ^ (i * 0x9E3779B9u);
        int  x  = inner_x + (int)(h % (uint32_t)inner_w);
        color_t c = COLORS[(h >> 16) & 3];
        int  yoff = ((h >> 20) & 3) - 1;   // -1..+2 wobble
        int  y = y_line + yoff;
        // 3-pixel cross for a sparkle glint.
        shrine_pixel(x,     y,     c);
        shrine_pixel(x - 1, y,     c);
        shrine_pixel(x + 1, y,     c);
        shrine_pixel(x,     y - 1, c);
        shrine_pixel(x,     y + 1, c);
    }
}

static void draw_static(void)
{
    shrine_clear(C_BG);
    shrine_window(0, 0, TEXT_COLS, TEXT_ROWS, C_YELLOW, C_BG);

    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 4, G_HLINE[0], C_YELLOW, C_BG);

    shrine_puts_centered(2,  "T E M P L E   S H R I N E", C_YELLOW,  C_BG);
    shrine_puts_centered(6,  "AN OFFICIAL GOD TEMPLE",    C_LTCYAN,  C_BG);

    // Memorial block above the logo so nothing gets clipped.
    shrine_puts_centered(8,  "IN MEMORY OF",   C_LTGRAY,  C_BG);
    shrine_puts_centered(9,  "TERRY A. DAVIS", C_LTGREEN, C_BG);
    shrine_puts_centered(10, "1969  -  2018", C_LTGREEN, C_BG);

    shrine_puts_centered(28, "PRESS ANY BUTTON TO ENTER THE TEMPLE",
                         C_LTGRAY, C_BG);

    draw_temple_logo();
}

static void draw_bats(int phase)
{
    shrine_fill_rect(BAT_L_X, BAT_BASE_Y, 8, BAT_BOX_H, PAL_RGB565[C_BG]);
    shrine_fill_rect(BAT_R_X, BAT_BASE_Y, 8, BAT_BOX_H, PAL_RGB565[C_BG]);
    int left_y  = BAT_BASE_Y + ((phase & 1) ? 0 : 2);
    int right_y = BAT_BASE_Y + ((phase & 1) ? 2 : 0);
    shrine_sprite8(BAT_L_X, left_y,  FONT8X8[(uint8_t)G_BAT[0]],
                   C_LTMAGENTA, C_BG);
    shrine_sprite8(BAT_R_X, right_y, FONT8X8[(uint8_t)G_BAT[0]],
                   C_LTMAGENTA, C_BG);
}

static void draw_word(const char *word)
{
    shrine_fill_rect(GLYPH_W, WORD_ROW * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, GLYPH_H,
                     PAL_RGB565[C_BG]);
    char line[64];
    snprintf(line, sizeof(line), "GOD SAYS:  %s", word);
    shrine_puts_centered(WORD_ROW, line, C_LTGREEN, C_BG);
}

static void draw_prompt(bool visible)
{
    shrine_fill_rect(GLYPH_W, PROMPT_ROW * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, GLYPH_H,
                     PAL_RGB565[C_BG]);
    if (visible) {
        shrine_puts_centered(PROMPT_ROW, "PRESS ANY KEY",
                             C_WHITE, C_BG);
    }
}

void splash_run(void)
{
    const char *god_word = VOCAB[shrine_god(VOCAB_N)];
    bool prompt = true;
    int  bat_phase = 0;
    int  sparkle_phase = 0;

    draw_static();
    draw_bats(bat_phase);
    draw_sparkle_divider(sparkle_phase);
    draw_word(god_word);
    draw_prompt(prompt);

    uint32_t last = shrine_ms();
    uint32_t blink_accum = 0, word_accum = 0, bat_accum = 0, sparkle_accum = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_any_pressed()) return;

        uint32_t now = shrine_ms();
        uint32_t dt = now - last; last = now;

        blink_accum   += dt;
        word_accum    += dt;
        bat_accum     += dt;
        sparkle_accum += dt;

        if (blink_accum >= 500) {
            blink_accum = 0;
            prompt = !prompt;
            draw_prompt(prompt);
        }
        if (word_accum >= 2000) {
            word_accum = 0;
            god_word = VOCAB[shrine_god(VOCAB_N)];
            draw_word(god_word);
        }
        if (bat_accum >= 350) {
            bat_accum = 0;
            bat_phase++;
            draw_bats(bat_phase);
        }
        if (sparkle_accum >= 220) {
            sparkle_accum = 0;
            sparkle_phase++;
            draw_sparkle_divider(sparkle_phase);
        }

        shrine_sleep_ms(20);
    }
}
