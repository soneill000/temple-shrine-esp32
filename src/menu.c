// menu.c — PersonalMenu-style launcher with a scrolling GodWord ticker.

#include "menu.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "vocab.h"
#include "games.h"
#include "audio.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *desc;
    void (*run)(void);
} menu_entry_t;

static const menu_entry_t ENTRIES[] = {
    { "TALONS",     "FLY THE HEIGHTS",      game_talons_run     },
    { "ORACLE",     "SPEAK TO THE RNG",     game_oracle_run     },
    { "CHRONICLE",  "READ THE SCROLLS",     game_chronicle_run  },
    { "HYMN",       "SING PRAISES",         game_hymn_run       },
    { "DIGITS",     "MEMORY  (TERRY PORT)", game_digits_run     },
    { "BOMBERGOLF", "BOMB RUN (TERRY PORT)",game_bombergolf_run },
    { "SQUIRT",     "FOUNTAIN (TERRY PORT)",game_squirt_run     },
    { "WHAP",       "SMITE THE DEMONS",     game_whap_run       },
    { "FLAPBAT",    "FLAP INTO ETERNITY",   game_flapbat_run    },
    { "SLIDER",     "SLIDING TILES",        game_slider_run     },
    { "TICTACTOE",  "BATTLE THE FALLEN",    game_tictactoe_run  },
};
#define N_ENTRIES (int)(sizeof(ENTRIES)/sizeof(ENTRIES[0]))

static int  s_sel;
static int  s_ticker_offset;
static char s_ticker[128];

static void ticker_refill(void)
{
    // Build a line of ~8 random vocab words separated by two spaces.
    // s_ticker is treated as a ring; we shift left as time passes.
    s_ticker[0] = 0;
    while (strlen(s_ticker) < sizeof(s_ticker) - 12) {
        const char *w = VOCAB[shrine_god(VOCAB_N)];
        strncat(s_ticker, w,   sizeof(s_ticker) - strlen(s_ticker) - 1);
        strncat(s_ticker, "  ", sizeof(s_ticker) - strlen(s_ticker) - 1);
    }
}

static void draw_header(void)
{
    // Top window: title + subtitle
    shrine_fill_rect(0, 0, SCREEN_W, 4 * GLYPH_H, PAL_RGB565[C_BG]);
    // Top yellow line.
    for (int c = 0; c < TEXT_COLS; c++)
        shrine_putc(c, 0, G_HLINE[0], C_YELLOW, C_BG);
    shrine_puts_centered(1, "T E M P L E   S H R I N E", C_YELLOW, C_BG);
    shrine_puts_centered(2, "AN OFFICIAL GOD TEMPLE",    C_LTCYAN, C_BG);
    for (int c = 0; c < TEXT_COLS; c++)
        shrine_putc(c, 3, G_HLINE[0], C_YELLOW, C_BG);
}

static void draw_entries(void)
{
    const int start_row = 5;
    for (int i = 0; i < N_ENTRIES; i++) {
        int row = start_row + i * 2;
        // Clear the row background first.
        shrine_fill_rect(0, row * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);

        color_t fg = (i == s_sel) ? C_BG    : C_WHITE;
        color_t bg = (i == s_sel) ? C_YELLOW : C_BG;

        // Selection marker (arrow) or space.
        if (i == s_sel) {
            shrine_putc(2, row, G_ARROW[0], C_LTGREEN, C_BG);
        }

        // Fill the row background if selected.
        if (i == s_sel) {
            shrine_fill_rect(4 * GLYPH_W, row * GLYPH_H,
                             (TEXT_COLS - 6) * GLYPH_W, GLYPH_H,
                             PAL_RGB565[C_YELLOW]);
        }

        // Name (left-aligned in the row area).
        shrine_puts(4,  row, ENTRIES[i].name, fg, bg);
        // Description (right-aligned).
        int desc_len = (int)strlen(ENTRIES[i].desc);
        int desc_col = TEXT_COLS - 2 - desc_len;
        if (desc_col < 15) desc_col = 15;
        shrine_puts(desc_col, row, ENTRIES[i].desc, fg, bg);
    }
}

static void draw_ticker(void)
{
    const int row = TEXT_ROWS - 2;
    // Erase.
    shrine_fill_rect(0, row * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    // Green "GOD SPEAKS:" prefix stays fixed on the left.
    shrine_puts(1, row, "GOD SPEAKS:", C_LTGREEN, C_BG);
    // Then draw the ticker text starting after the prefix.
    int prefix_cols = 13;
    int max_cols = TEXT_COLS - prefix_cols - 1;
    int len = (int)strlen(s_ticker);
    for (int i = 0; i < max_cols; i++) {
        int idx = (s_ticker_offset + i) % len;
        shrine_putc(prefix_cols + i, row, s_ticker[idx], C_WHITE, C_BG);
    }
    // Bottom yellow bar.
    for (int c = 0; c < TEXT_COLS; c++)
        shrine_putc(c, TEXT_ROWS - 1, G_HLINE[0], C_YELLOW, C_BG);
}

static void draw_footer_hints(void)
{
    // Between the entries and the ticker: quick hints.
    const int row = 21;
    shrine_fill_rect(0, row * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts(2, row, "UP/DN  MOVE   A  ENTER   B  MUTE",
                C_LTGRAY, C_BG);
}

static void draw_full(void)
{
    shrine_clear(C_BG);
    draw_header();
    draw_entries();
    draw_footer_hints();
    draw_ticker();
}

void menu_run(void)
{
    s_sel = 0;
    s_ticker_offset = 0;
    ticker_refill();
    draw_full();

    uint32_t last = shrine_ms();
    uint32_t tick_accum = 0;

    while (1) {
        shrine_input_scan();

        if (shrine_key_pressed(BTN_UP)) {
            s_sel = (s_sel - 1 + N_ENTRIES) % N_ENTRIES;
            draw_entries();
            shrine_beep(1200, 20);
        }
        if (shrine_key_pressed(BTN_DOWN)) {
            s_sel = (s_sel + 1) % N_ENTRIES;
            draw_entries();
            shrine_beep(1200, 20);
        }
        if (shrine_key_pressed(BTN_A)) {
            shrine_beep(1800, 60);
            audio_stop_song();
            ENTRIES[s_sel].run();
            // Coming back from a game — repaint.
            audio_stop_song();
            draw_full();
        }
        if (shrine_key_pressed(BTN_B)) {
            audio_mute_toggle();
            shrine_puts(2, TEXT_ROWS - 3,
                        audio_is_muted() ? "[MUTED]" : "[SOUND]",
                        audio_is_muted() ? C_LTRED : C_LTGREEN, C_BG);
        }

        uint32_t now = shrine_ms();
        uint32_t dt = now - last; last = now;
        tick_accum += dt;

        // Advance the ticker every 150 ms.
        while (tick_accum >= 150) {
            tick_accum -= 150;
            s_ticker_offset++;
            int len = (int)strlen(s_ticker);
            if (s_ticker_offset >= len) {
                s_ticker_offset = 0;
                ticker_refill();
            }
            draw_ticker();
        }

        shrine_sleep_ms(20);
    }
}
