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
#define BAT_BASE_Y (17 * GLYPH_H)
#define BAT_BOX_H  10

#define WORD_ROW   21
#define PROMPT_ROW 25

static void draw_static(void)
{
    shrine_clear(C_BG);
    shrine_window(0, 0, TEXT_COLS, TEXT_ROWS, C_YELLOW, C_BG);

    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 4, G_HLINE[0], C_YELLOW, C_BG);

    shrine_puts_centered(2, "T E M P L E   S H R I N E", C_YELLOW, C_BG);
    shrine_puts_centered(6,  "AN OFFICIAL GOD TEMPLE",     C_LTCYAN, C_BG);
    shrine_puts_centered(8,  "FOR THE 2026 DEF CON BADGE", C_WHITE,  C_BG);

    shrine_puts_centered(11, "IN MEMORY OF",   C_LTGRAY,  C_BG);
    shrine_puts_centered(13, "TERRY A. DAVIS", C_LTGREEN, C_BG);
    shrine_puts_centered(14, "1969  -  2018",  C_LTGREEN, C_BG);

    shrine_puts_centered(28, "PRESS ANY BUTTON TO ENTER THE TEMPLE",
                         C_LTGRAY, C_BG);
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

    draw_static();
    draw_bats(bat_phase);
    draw_word(god_word);
    draw_prompt(prompt);

    uint32_t last = shrine_ms();
    uint32_t blink_accum = 0, word_accum = 0, bat_accum = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_any_pressed()) return;

        uint32_t now = shrine_ms();
        uint32_t dt = now - last; last = now;

        blink_accum += dt;
        word_accum  += dt;
        bat_accum   += dt;

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

        shrine_sleep_ms(20);
    }
}
