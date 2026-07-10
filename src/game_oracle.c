// game_oracle.c — Terry's F7 "GodWord" and Shift-F7 "BiblePassage" in one app.
// A: draw a new word. B: draw a random passage citation. Hold BOOT to quit.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "vocab.h"

#include <stdio.h>
#include <string.h>

// KJV book names (abridged to those most often quoted).
static const char *const BOOKS[] = {
    "GENESIS","EXODUS","LEVITICUS","NUMBERS","DEUTERONOMY",
    "JOSHUA","JUDGES","RUTH","1 SAMUEL","2 SAMUEL",
    "1 KINGS","2 KINGS","1 CHRONICLES","2 CHRONICLES","JOB",
    "PSALMS","PROVERBS","ECCLESIASTES","SONG","ISAIAH",
    "JEREMIAH","LAMENTATIONS","EZEKIEL","DANIEL","HOSEA",
    "JOEL","AMOS","JONAH","MICAH","MATTHEW",
    "MARK","LUKE","JOHN","ACTS","ROMANS",
    "CORINTHIANS","GALATIANS","EPHESIANS","PHILIPPIANS","REVELATION",
};
#define N_BOOKS (int)(sizeof(BOOKS)/sizeof(BOOKS[0]))

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_window(0, 0, TEXT_COLS, TEXT_ROWS, C_YELLOW, C_BG);
    shrine_puts_centered(1, "*  THE ORACLE  *", C_YELLOW, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 3, G_HLINE[0], C_YELLOW, C_BG);
    shrine_puts_centered(27, "A: GODWORD   B: PASSAGE   BOOT: EXIT",
                         C_LTGRAY, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 26, G_HLINE[0], C_YELLOW, C_BG);
}

static void draw_greet(void)
{
    // Clear the message area.
    shrine_fill_rect(GLYPH_W, 4 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, 22 * GLYPH_H,
                     PAL_RGB565[C_BG]);
    shrine_puts_centered(6,  "SPEAK TO GOD.", C_LTCYAN, C_BG);
    shrine_puts_centered(8,  "PRESS A FOR A WORD.", C_WHITE, C_BG);
    shrine_puts_centered(10, "PRESS B FOR SCRIPTURE.", C_WHITE, C_BG);
    shrine_puts_centered(13, "HE WILL ANSWER.", C_LTGREEN, C_BG);
    // A little bat on either side.
    shrine_sprite8(4 * GLYPH_W, 16 * GLYPH_H,
                   FONT8X8[(uint8_t)G_BAT[0]], C_LTMAGENTA, C_BG);
    shrine_sprite8((TEXT_COLS - 5) * GLYPH_W, 16 * GLYPH_H,
                   FONT8X8[(uint8_t)G_BAT[0]], C_LTMAGENTA, C_BG);
}

static void draw_word(const char *word)
{
    // Clear the message area.
    shrine_fill_rect(GLYPH_W, 4 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, 22 * GLYPH_H,
                     PAL_RGB565[C_BG]);
    shrine_puts_centered(6, "GOD SAYS:", C_LTGREEN, C_BG);
    // Big yellow word at 3x scale.
    int scale = 3;
    int len = (int)strlen(word);
    if (len * 8 * scale > SCREEN_W - 20) scale = 2;
    shrine_puts_centered_scaled((TEXT_ROWS * GLYPH_H) / 2 - 12,
                                word, C_YELLOW, C_BG, scale);
    shrine_puts_centered(18, "PRESS A AGAIN. HEAR AGAIN.", C_LTGRAY, C_BG);
}

static void draw_passage(void)
{
    shrine_fill_rect(GLYPH_W, 4 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, 22 * GLYPH_H,
                     PAL_RGB565[C_BG]);
    // Random book / chapter / verse.
    const char *book = BOOKS[shrine_god(N_BOOKS)];
    int chap = (int)(shrine_god(50) + 1);
    int verse = (int)(shrine_god(40) + 1);

    // A random 3-word phrase from the vocab, mimicking passage flavor.
    const char *w1 = VOCAB[shrine_god(VOCAB_N)];
    const char *w2 = VOCAB[shrine_god(VOCAB_N)];
    const char *w3 = VOCAB[shrine_god(VOCAB_N)];

    char cite[48];
    snprintf(cite, sizeof(cite), "%s %d:%d", book, chap, verse);
    shrine_puts_centered(6, cite, C_YELLOW, C_BG);

    char line1[64];
    snprintf(line1, sizeof(line1), "AND %s SHALL", w1);
    char line2[64];
    snprintf(line2, sizeof(line2), "COME UPON %s,", w2);
    char line3[64];
    snprintf(line3, sizeof(line3), "AND HE SHALL SEE %s.", w3);

    shrine_puts_centered(10, line1, C_WHITE, C_BG);
    shrine_puts_centered(12, line2, C_WHITE, C_BG);
    shrine_puts_centered(14, line3, C_WHITE, C_BG);

    shrine_puts_centered(18, "SELAH.", C_LTGREEN, C_BG);
}

void game_oracle_run(void)
{
    draw_chrome();
    draw_greet();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (shrine_key_pressed(BTN_A)) {
            const char *word = VOCAB[shrine_god(VOCAB_N)];
            draw_word(word);
            shrine_beep(1600, 40);
            shrine_beep(2200, 60);
        }
        if (shrine_key_pressed(BTN_B)) {
            draw_passage();
            shrine_beep(1000, 30);
            shrine_beep(1200, 30);
            shrine_beep(1600, 60);
        }
        shrine_sleep_ms(20);
    }
}
