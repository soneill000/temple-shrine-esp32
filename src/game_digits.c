// game_digits.c — faithful port of Terry's Digits.HC.
//
// Terry's flow is trivially short:
//   1. Show intro + color code, wait key
//   2. loop:
//        Clear screen, print "Pattern / Length: N+1"
//        Append random 0-9 to the sequence, print sequence
//        Wait key
//        Clear screen, print "Guess / Length: N+1"
//        For each digit in sequence: GetChar, print it
//          If wrong: print "Score: X", show pattern, beep beep, wait key,
//                    restart from an empty sequence
//
// Adaptation: our badge has no keyboard, so instead of GetChar for a digit
// we use a d-pad picker (LEFT/RIGHT moves cursor over 0-9, A confirms).
// Everything else follows Terry's structure verbatim.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

// ST_RAINBOW_10 approximation — resistor color code, mapped to our 16-color
// VGA palette. Black substituted with LTGRAY so 0 is legible on blue.
static const color_t DIGIT_COLOR[10] = {
    C_LTGRAY,    // 0
    C_BROWN,     // 1
    C_LTRED,     // 2 red
    C_YELLOW,    // 3 orange sub
    C_LTGREEN,   // 4
    C_GREEN,     // 5
    C_LTCYAN,    // 6 blue sub
    C_LTMAGENTA, // 7 violet
    C_DKGRAY,    // 8
    C_WHITE,     // 9
};

#define MAX_PATTERN 32

static void draw_frame(void)
{
    shrine_clear(C_BG);
    shrine_window(0, 0, TEXT_COLS, TEXT_ROWS, C_YELLOW, C_BG);
    shrine_puts_centered(1, "*  DIGITS  *", C_YELLOW, C_BG);
    shrine_puts_centered(28, "L/R PICK  A ENTER  BOOT EXIT",
                         C_LTGRAY, C_BG);
}

static void draw_colored_row(int row, uint8_t *seq, int n)
{
    // Center a row of colored digits like Terry's PrintPattern.
    int scale = 2;
    if (n > 10) scale = 1;
    int digit_w = 8 * scale;
    int gap = scale;
    int total = n * (digit_w + gap) - gap;
    int start_x = (SCREEN_W - total) / 2;
    int y = row * GLYPH_H;
    for (int i = 0; i < n; i++) {
        char c[2] = { (char)('0' + seq[i]), 0 };
        shrine_putsxy_scaled(start_x + i * (digit_w + gap), y,
                             c, DIGIT_COLOR[seq[i]], C_BG, scale);
    }
}

static void draw_picker(int selector)
{
    int row = 22;
    shrine_fill_rect(0, row * GLYPH_H, SCREEN_W, 3 * GLYPH_H,
                     PAL_RGB565[C_BG]);
    int scale = 2;
    int digit_w = 8 * scale;
    int gap = 4;
    int total = 10 * (digit_w + gap) - gap;
    int start_x = (SCREEN_W - total) / 2;
    int y = row * GLYPH_H;
    for (int i = 0; i < 10; i++) {
        char c[2] = { (char)('0' + i), 0 };
        int px = start_x + i * (digit_w + gap);
        color_t bg = (i == selector) ? C_YELLOW : C_BG;
        if (i == selector) {
            shrine_fill_rect(px - 2, y - 2, digit_w + 4, digit_w + 4,
                             PAL_RGB565[C_YELLOW]);
        }
        shrine_putsxy_scaled(px, y, c, DIGIT_COLOR[i], bg, scale);
    }
}

static void draw_intro(void)
{
    draw_frame();
    shrine_puts_centered(4, "MEMORY GAME.", C_LTCYAN, C_BG);
    shrine_puts_centered(6, "REMEMBER THE DIGITS,", C_WHITE, C_BG);
    shrine_puts_centered(7, "THEN REPRODUCE THEM.", C_WHITE, C_BG);
    shrine_puts_centered(9, "COLORS FOLLOW THE", C_LTGRAY, C_BG);
    shrine_puts_centered(10, "RESISTOR CODE:", C_LTGRAY, C_BG);
    // Print each digit in its color, three per row.
    for (int i = 0; i < 10; i++) {
        char c[2] = { (char)('0' + i), 0 };
        int col = 8 + (i % 5) * 5;
        int row = 12 + (i / 5) * 2;
        shrine_puts(col, row, c, DIGIT_COLOR[i], C_BG);
    }
    shrine_puts_centered(18, "PRESS A TO BEGIN", C_YELLOW, C_BG);
}

static void draw_pattern_screen(uint8_t *pattern, int n)
{
    draw_frame();
    char buf[24];
    snprintf(buf, sizeof(buf), "PATTERN   LENGTH %d", n);
    shrine_puts_centered(4, buf, C_LTCYAN, C_BG);
    draw_colored_row(10, pattern, n);
    shrine_puts_centered(24, "PRESS A WHEN READY", C_YELLOW, C_BG);
}

static void draw_guess_screen(int n)
{
    draw_frame();
    char buf[24];
    snprintf(buf, sizeof(buf), "GUESS   LENGTH %d", n);
    shrine_puts_centered(4, buf, C_LTCYAN, C_BG);
    // Empty slots row
    shrine_puts_centered(10, "_ _ _ _ _ _ _ _ _", C_LTGRAY, C_BG);
}

static void draw_guess_progress(uint8_t *guess, int filled, int n)
{
    // Redraw the progress row with entered digits + underscores.
    shrine_fill_rect(0, 10 * GLYPH_H, SCREEN_W, 2 * GLYPH_H, PAL_RGB565[C_BG]);
    // Use same scaling as pattern for consistency.
    int scale = 2;
    if (n > 10) scale = 1;
    int digit_w = 8 * scale;
    int gap = scale;
    int total = n * (digit_w + gap) - gap;
    int start_x = (SCREEN_W - total) / 2;
    int y = 10 * GLYPH_H;
    for (int i = 0; i < n; i++) {
        int px = start_x + i * (digit_w + gap);
        if (i < filled) {
            char c[2] = { (char)('0' + guess[i]), 0 };
            shrine_putsxy_scaled(px, y, c, DIGIT_COLOR[guess[i]], C_BG, scale);
        } else {
            shrine_putsxy_scaled(px, y, "_", C_LTGRAY, C_BG, scale);
        }
    }
}

static void wait_for_a(void)
{
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;
        shrine_sleep_ms(20);
    }
}

void game_digits_run(void)
{
    uint8_t pattern[MAX_PATTERN];
    uint8_t guess[MAX_PATTERN];

    draw_intro();
    wait_for_a();
    if (shrine_should_quit()) return;

restart:
    memset(pattern, 0, sizeof(pattern));
    int len = 0;

    while (1) {
        // Grow the sequence.
        if (len >= MAX_PATTERN) return;   // won everything
        pattern[len++] = (uint8_t)shrine_god(10);

        // Show the pattern.
        draw_pattern_screen(pattern, len);
        wait_for_a();
        if (shrine_should_quit()) return;

        // Guess phase.
        draw_guess_screen(len);
        draw_guess_progress(guess, 0, len);
        int selector = 0;
        draw_picker(selector);

        for (int i = 0; i < len; i++) {
            // Digit input via picker.
            bool confirmed = false;
            while (!confirmed) {
                shrine_input_scan();
                if (shrine_should_quit()) return;
                if (shrine_key_pressed(BTN_LEFT)) {
                    selector = (selector - 1 + 10) % 10;
                    draw_picker(selector);
                    shrine_beep(1400, 15);
                }
                if (shrine_key_pressed(BTN_RIGHT)) {
                    selector = (selector + 1) % 10;
                    draw_picker(selector);
                    shrine_beep(1400, 15);
                }
                if (shrine_key_pressed(BTN_A)) {
                    confirmed = true;
                    int hz = 440 + selector * 80;
                    shrine_beep(hz, 100);
                }
                shrine_sleep_ms(20);
            }

            guess[i] = (uint8_t)selector;
            draw_guess_progress(guess, i + 1, len);

            if (guess[i] != pattern[i]) {
                // Wrong — show the score, reveal the pattern, beep beep.
                draw_frame();
                char buf[24];
                snprintf(buf, sizeof(buf), "SCORE %d", len - 1);
                shrine_puts_centered(4, buf, C_LTRED, C_BG);
                shrine_puts_centered(8, "THE PATTERN WAS:", C_LTCYAN, C_BG);
                draw_colored_row(12, pattern, len);
                shrine_puts_centered(20, "PRESS A TO TRY AGAIN", C_YELLOW, C_BG);
                shrine_beep(300, 200);
                shrine_beep(200, 300);
                wait_for_a();
                if (shrine_should_quit()) return;
                goto restart;
            }
        }
        // Round completed — brief flourish before growing the sequence.
        shrine_beep(2000, 80);
        shrine_beep(2400, 100);
        shrine_sleep_ms(300);
    }
}
