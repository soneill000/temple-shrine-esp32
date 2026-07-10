// splash.c — TempleOS-style Welcome.DD splash.
// Deep blue background, yellow title, cyan sub-heading, blinking prompt,
// a random GodWord "God says: X" line, and a small bobbing bat sprite.

#include "splash.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "vocab.h"

#include <stdio.h>

static void draw_frame(uint32_t t_ms, const char *god_word,
                       bool prompt_visible, int bat_phase)
{
    shrine_clear(C_BG);

    // Outer border in yellow (Terry chrome).
    shrine_window(0, 0, TEXT_COLS, TEXT_ROWS, C_YELLOW, C_BG);

    // Inner header divider (row 4) — a horizontal line.
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 4, G_HLINE[0], C_YELLOW, C_BG);

    // Title.
    shrine_puts_centered(2, "T E M P L E   S H R I N E", C_YELLOW, C_BG);

    // Sub-heading.
    shrine_puts_centered(6,  "AN OFFICIAL GOD TEMPLE",     C_LTCYAN, C_BG);
    shrine_puts_centered(8,  "FOR THE 2024 DEF CON BADGE", C_WHITE,  C_BG);

    shrine_puts_centered(11, "IN MEMORY OF", C_LTGRAY, C_BG);
    shrine_puts_centered(13, "TERRY A. DAVIS",  C_LTGREEN, C_BG);
    shrine_puts_centered(14, "1969  -  2018",   C_LTGREEN, C_BG);

    // Bat sprite bobbing near the bottom-left corner.
    int bat_y = 17 * GLYPH_H + (bat_phase & 1 ? 0 : 2);
    shrine_sprite8(6 * GLYPH_W, bat_y, FONT8X8[(uint8_t)G_BAT[0]],
                   C_LTMAGENTA, C_BG);
    // A second bat on the right, out of phase.
    shrine_sprite8(SCREEN_W - 7 * GLYPH_W,
                   17 * GLYPH_H + (bat_phase & 1 ? 2 : 0),
                   FONT8X8[(uint8_t)G_BAT[0]],
                   C_LTMAGENTA, C_BG);

    // "God says: X" — the signature GodWord line.
    char line[64];
    snprintf(line, sizeof(line), "GOD SAYS:  %s", god_word);
    shrine_puts_centered(21, line, C_LTGREEN, C_BG);

    // Blinking prompt.
    shrine_puts_centered(25, prompt_visible ? "PRESS ANY KEY TO ENTER" : "                      ",
                         C_WHITE, C_BG);

    // Copy footer.
    shrine_puts_centered(28, "PRESS BOOT TO SILENCE / RESTART",
                         C_LTGRAY, C_BG);
}

void splash_run(void)
{
    const char *god_word = VOCAB[shrine_god(VOCAB_N)];
    uint32_t last = shrine_ms();
    uint32_t blink_accum = 0, word_accum = 0, bat_accum = 0;
    bool prompt = true;
    int bat_phase = 0;

    draw_frame(0, god_word, prompt, bat_phase);

    while (1) {
        shrine_input_scan();

        // Any key exits.
        if (shrine_any_pressed()) return;

        uint32_t now = shrine_ms();
        uint32_t dt = now - last; last = now;

        blink_accum += dt;
        word_accum  += dt;
        bat_accum   += dt;

        bool need_draw = false;

        if (blink_accum >= 500) {
            blink_accum = 0;
            prompt = !prompt;
            need_draw = true;
        }
        if (word_accum >= 2000) {
            word_accum = 0;
            god_word = VOCAB[shrine_god(VOCAB_N)];
            need_draw = true;
        }
        if (bat_accum >= 250) {
            bat_accum = 0;
            bat_phase++;
            need_draw = true;
        }

        if (need_draw) draw_frame(now, god_word, prompt, bat_phase);

        shrine_sleep_ms(20);
    }
}
