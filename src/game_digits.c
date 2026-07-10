// game_digits.c — port of Terry's Digits.HC (TempleOS Demo/Games/Digits.HC).
//
// Terry's original: keyboard-driven Simon-Says with digits colored per the
// electrical-engineering resistor color code (ST_RAINBOW_10). Type back the
// sequence exactly.
//
// Badge adaptation: we only have a d-pad + A/B, so we replace typing with a
// digit selector — a row of 0-9 with a cursor. LEFT/RIGHT moves the cursor,
// A confirms. Otherwise, the game logic is the same as Terry's.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

// 10-color rainbow — approximates ST_RAINBOW_10 within our 16-color VGA
// palette. Order roughly follows resistor-code (black→brown→red→orange→
// yellow→green→blue→violet→gray→white); we substitute nearest palette
// members where the exact color isn't available.
static const color_t DIGIT_COLOR[10] = {
    C_LTGRAY,    // 0 (black is unreadable on blue bg — sub for grey)
    C_BROWN,     // 1
    C_LTRED,     // 2 (red)
    C_YELLOW,    // 3 (as orange sub)
    C_LTGREEN,   // 4
    C_GREEN,     // 5
    C_LTCYAN,    // 6 (as blue sub — plain BLUE is bg)
    C_LTMAGENTA, // 7 (violet)
    C_DKGRAY,    // 8
    C_WHITE,     // 9
};

#define MAX_PATTERN 40

static uint8_t s_pattern[MAX_PATTERN];   // digit sequence (0-9)
static int     s_len;                    // current pattern length
static int     s_selector;               // cursor over 0-9
static int     s_best;                   // best (longest completed) run

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_window(0, 0, TEXT_COLS, TEXT_ROWS, C_YELLOW, C_BG);
    shrine_puts_centered(1, "*  DIGITS  *", C_YELLOW, C_BG);
    shrine_puts_centered(2, "REMEMBER AND REPRODUCE",
                         C_LTCYAN, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 3, G_HLINE[0], C_YELLOW, C_BG);
    shrine_puts_centered(TEXT_ROWS - 2,
                         "LEFT/RIGHT PICK   A ENTER   BOOT EXIT",
                         C_LTGRAY, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, TEXT_ROWS - 3, G_HLINE[0], C_YELLOW, C_BG);
}

// Draw a legend of the 10 rainbow colors at a fixed row.
static void draw_legend(int row)
{
    shrine_puts(3, row, "COLOR CODE:", C_LTCYAN, C_BG);
    int x = 3 * GLYPH_W + 12 * GLYPH_W;
    for (int i = 0; i < 10; i++) {
        char c[2] = { (char)('0' + i), 0 };
        shrine_putsxy(x + i * (GLYPH_W + 2), row * GLYPH_H,
                      c, DIGIT_COLOR[i], C_BG);
    }
}

// Erase the pattern row.
static void clear_pattern_area(void)
{
    shrine_fill_rect(GLYPH_W, 5 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, 12 * GLYPH_H,
                     PAL_RGB565[C_BG]);
}

// Show the pattern at scale=3 centered on screen.
static void draw_pattern(int visible_len)
{
    clear_pattern_area();
    shrine_puts_centered(5, "PATTERN", C_YELLOW, C_BG);
    if (visible_len <= 0) return;

    int scale = 3;
    int digit_w = 8 * scale;
    int total_w = visible_len * (digit_w + 4);
    if (total_w > SCREEN_W - 16) {
        scale = 2;
        digit_w = 8 * scale;
        total_w = visible_len * (digit_w + 3);
    }
    int start_x = (SCREEN_W - total_w) / 2;
    int y = 9 * GLYPH_H;
    for (int i = 0; i < visible_len; i++) {
        char c[2] = { (char)('0' + s_pattern[i]), 0 };
        shrine_putsxy_scaled(start_x + i * (digit_w + 3), y,
                             c, DIGIT_COLOR[s_pattern[i]], C_BG, scale);
    }
}

// Draw the digit picker row (0-9 with cursor over s_selector).
static void draw_picker(void)
{
    int row = 20;
    shrine_fill_rect(0, row * GLYPH_H, SCREEN_W, 3 * GLYPH_H,
                     PAL_RGB565[C_BG]);
    shrine_puts_centered(20, "YOUR CHOICE:", C_LTCYAN, C_BG);
    // Cursor row + selection
    int scale = 2;
    int digit_w = 8 * scale;
    int gap = 4;
    int total_w = 10 * (digit_w + gap) - gap;
    int start_x = (SCREEN_W - total_w) / 2;
    int y = 22 * GLYPH_H;
    for (int i = 0; i < 10; i++) {
        char c[2] = { (char)('0' + i), 0 };
        int px = start_x + i * (digit_w + gap);
        if (i == s_selector) {
            // Yellow background under selected digit
            shrine_fill_rect(px - 2, y - 2, digit_w + 4, digit_w + 4,
                             PAL_RGB565[C_YELLOW]);
            shrine_putsxy_scaled(px, y, c, DIGIT_COLOR[i], C_YELLOW, scale);
        } else {
            shrine_putsxy_scaled(px, y, c, DIGIT_COLOR[i], C_BG, scale);
        }
    }
}

static void draw_status(const char *line, color_t c)
{
    shrine_fill_rect(GLYPH_W, 6 * GLYPH_H, (TEXT_COLS - 2) * GLYPH_W,
                     GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(6, line, c, C_BG);
}

static void draw_best(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "BEST: %d", s_best);
    shrine_fill_rect(GLYPH_W, 25 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(25, buf, C_LTGREEN, C_BG);
}

// Play the pattern with beeps + flashing digits, then hide.
static void play_pattern(void)
{
    draw_status("WATCH...", C_YELLOW);
    for (int i = 0; i < s_len; i++) {
        s_pattern[i] = s_pattern[i];   // already set
    }
    // Reveal one digit at a time.
    for (int i = 0; i < s_len; i++) {
        draw_pattern(i + 1);
        int hz = 440 + s_pattern[i] * 80;
        shrine_beep(hz, 200);
        shrine_sleep_ms(120);
    }
    shrine_sleep_ms(600);
    clear_pattern_area();
    draw_status("NOW YOU:", C_LTGREEN);
}

static void reset_game(void)
{
    s_len = 0;
    s_selector = 0;
    memset(s_pattern, 0, sizeof(s_pattern));
}

static void add_random_digit(void)
{
    if (s_len < MAX_PATTERN) {
        s_pattern[s_len++] = (uint8_t)shrine_god(10);
    }
}

void game_digits_run(void)
{
    reset_game();
    s_best = 0;
    draw_chrome();
    draw_legend(24);
    draw_best();

    add_random_digit();
    play_pattern();
    draw_picker();

    int guessed = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (shrine_key_pressed(BTN_LEFT)) {
            s_selector = (s_selector - 1 + 10) % 10;
            draw_picker();
            shrine_beep(1400, 20);
        }
        if (shrine_key_pressed(BTN_RIGHT)) {
            s_selector = (s_selector + 1) % 10;
            draw_picker();
            shrine_beep(1400, 20);
        }
        if (shrine_key_pressed(BTN_A)) {
            int hz = 440 + s_selector * 80;
            shrine_beep(hz, 120);

            if ((uint8_t)s_selector == s_pattern[guessed]) {
                guessed++;
                if (guessed == s_len) {
                    // Round complete.
                    if (s_len > s_best) { s_best = s_len; draw_best(); }
                    shrine_sleep_ms(200);
                    shrine_beep(2000, 80);
                    shrine_beep(2400, 100);
                    add_random_digit();
                    guessed = 0;
                    play_pattern();
                    draw_picker();
                }
            } else {
                // Wrong.
                draw_status("WRONG.", C_LTRED);
                shrine_beep(300, 250);
                shrine_beep(200, 350);
                draw_pattern(s_len);
                shrine_sleep_ms(2000);
                // Restart.
                reset_game();
                add_random_digit();
                guessed = 0;
                play_pattern();
                draw_picker();
            }
        }
        shrine_sleep_ms(20);
    }
}
