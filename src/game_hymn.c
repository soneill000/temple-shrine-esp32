// game_hymn.c — cycle through public-domain hymns; piezo plays them in a
// background task. A: play/pause. UP/DOWN: change hymn. B: mute toggle.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "audio.h"
#include "hymns.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char    *title;
    const char    *subtitle;
    const note_t  *notes;
    int            count;
} hymn_t;

static const hymn_t HYMNS[] = {
    { "OLD HUNDREDTH",     "THE DOXOLOGY (1551)",
      HYMN_OLD_HUNDREDTH,
      sizeof(HYMN_OLD_HUNDREDTH)/sizeof(HYMN_OLD_HUNDREDTH[0]) },
    { "AMAZING GRACE",     "JOHN NEWTON (1779)",
      HYMN_AMAZING_GRACE,
      sizeof(HYMN_AMAZING_GRACE)/sizeof(HYMN_AMAZING_GRACE[0]) },
    { "NEARER MY GOD",     "SARAH F. ADAMS (1856)",
      HYMN_NEARER_MY_GOD,
      sizeof(HYMN_NEARER_MY_GOD)/sizeof(HYMN_NEARER_MY_GOD[0]) },
};
#define N_HYMNS (int)(sizeof(HYMNS)/sizeof(HYMNS[0]))

static int  s_sel;
static bool s_playing;

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  HYMN PLAYER  *", C_YELLOW, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 3, G_HLINE[0], C_YELLOW, C_BG);
    shrine_puts_centered(28,
        "UP/DN CHANGE   A PLAY/STOP   B MUTE   BOOT EXIT",
        C_LTGRAY, C_BG);
}

static void draw_now_playing(void)
{
    // Clear body.
    shrine_fill_rect(GLYPH_W, 5 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, 20 * GLYPH_H,
                     PAL_RGB565[C_BG]);

    shrine_puts_centered(6, "NOW OPEN TO HYMN", C_LTCYAN, C_BG);

    char n[8];
    snprintf(n, sizeof(n), "%d / %d", s_sel + 1, N_HYMNS);
    shrine_puts_centered(8, n, C_LTGREEN, C_BG);

    shrine_puts_centered_scaled(10 * GLYPH_H, HYMNS[s_sel].title,
                                C_YELLOW, C_BG, 2);
    shrine_puts_centered(15, HYMNS[s_sel].subtitle, C_WHITE, C_BG);

    shrine_puts_centered(20,
        s_playing ? "* PLAYING *" : "( STOPPED )",
        s_playing ? C_LTGREEN : C_LTGRAY, C_BG);

    shrine_puts_centered(22,
        audio_is_muted() ? "AUDIO MUTED - PRESS B" : "",
        C_LTRED, C_BG);
}

void game_hymn_run(void)
{
    s_sel = 0;
    s_playing = false;
    audio_stop_song();

    draw_chrome();
    draw_now_playing();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) { audio_stop_song(); return; }

        if (shrine_key_pressed(BTN_UP)) {
            s_sel = (s_sel - 1 + N_HYMNS) % N_HYMNS;
            if (s_playing)
                audio_play_song(HYMNS[s_sel].notes, HYMNS[s_sel].count, true);
            draw_now_playing();
        }
        if (shrine_key_pressed(BTN_DOWN)) {
            s_sel = (s_sel + 1) % N_HYMNS;
            if (s_playing)
                audio_play_song(HYMNS[s_sel].notes, HYMNS[s_sel].count, true);
            draw_now_playing();
        }
        if (shrine_key_pressed(BTN_A)) {
            s_playing = !s_playing;
            if (s_playing)
                audio_play_song(HYMNS[s_sel].notes, HYMNS[s_sel].count, true);
            else
                audio_stop_song();
            draw_now_playing();
        }
        if (shrine_key_pressed(BTN_B)) {
            audio_mute_toggle();
            draw_now_playing();
        }

        shrine_sleep_ms(30);
    }
}
