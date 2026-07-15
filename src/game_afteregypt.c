// game_afteregypt.c — After Egypt v0.
//
// Terry's After Egypt is a menu-driven Moses experience with 9 scene
// modules (HoldCourt / TalkWithGod / ViewClouds / ViewMap / BreakCamp /
// WaterRock / Battle / Quail / Mt Horeb). This is the MVP shell: the
// main menu, the HoldCourt scene fully playable, and stubs for the
// other 8 so the launcher entry works and we can flesh them out
// module-by-module in follow-up commits.
//
// HoldCourt is the smallest and most self-contained scene in Terry's
// source. It's a Moses-as-judge mini-game: a random case is described
// (accused / crime / optional victim) and the player chooses one of
// three verdicts. Perfect first port.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "vocab.h"

#include <stdio.h>
#include <string.h>

// --- Scene identifiers ---
typedef enum {
    SC_COURT = 0,
    SC_GOD,
    SC_CLOUDS,
    SC_MAP,
    SC_CAMP,
    SC_WATER,
    SC_BATTLE,
    SC_QUAIL,
    SC_HOREB,
    SC_N,
} scene_t;

typedef struct { const char *name; scene_t id; } menu_entry_t;

static const menu_entry_t MENU[] = {
    { "HOLD COURT",    SC_COURT  },
    { "TALK WITH GOD", SC_GOD    },
    { "VIEW CLOUDS",   SC_CLOUDS },
    { "VIEW MAP",      SC_MAP    },
    { "BREAK CAMP",    SC_CAMP   },
    { "WATER ROCK",    SC_WATER  },
    { "BATTLE",        SC_BATTLE },
    { "QUAIL",         SC_QUAIL  },
    { "MT HOREB",      SC_HOREB  },
};
#define N_MENU (int)(sizeof(MENU) / sizeof(MENU[0]))

static int s_sel;

// --- Menu draw ---
static void draw_menu(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  AFTER EGYPT  *", C_YELLOW, C_BG);
    shrine_puts_centered(3, "IN MEMORY OF TERRY", C_LTCYAN, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 5, G_HLINE[0], C_YELLOW, C_BG);

    int start_row = 7;
    for (int i = 0; i < N_MENU; i++) {
        int row = start_row + i * 2;
        color_t fg = (i == s_sel) ? C_BG : C_WHITE;
        color_t bg = (i == s_sel) ? C_YELLOW : C_BG;
        if (i == s_sel) {
            shrine_fill_rect(0, row * GLYPH_H, SCREEN_W, GLYPH_H,
                             PAL_RGB565[C_YELLOW]);
            shrine_putc(4, row, G_ARROW[0], C_LTGREEN, C_BG);
        }
        shrine_puts(6, row, MENU[i].name, fg, bg);
    }
    shrine_puts_centered(TEXT_ROWS - 1,
                         "UP/DN MOVE  A ENTER  BOOT EXIT",
                         C_LTGRAY, C_BG);
}

// --- HoldCourt scene ---
// Terry's mechanic: pick a random accused, crime, victim, then present
// three verdict choices to the player. Original strings here are my own
// wording; the mechanic is ported.

static const char *ACCUSED[] = {
    "A MAN", "A WOMAN", "A CHILD",
};
static const char *CRIME[] = {
    "IS ACCUSED OF MURDER",
    "IS ACCUSED OF ADULTERY",
    "IS ACCUSED OF BLASPHEMY",
    "IS ACCUSED OF IDOLATRY",
};
static const char *VICTIM[] = {
    "TOWARDS A MAN.",
    "TOWARDS A WOMAN.",
    "TOWARDS A CHILD.",
    "TOWARDS A BEAST.",
};

static void draw_court_frame(const char *l1, const char *l2, bool with_choices)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  HOLD COURT  *", C_YELLOW, C_BG);
    shrine_puts_centered(3, "MOSES HEARS A CASE", C_LTCYAN, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 5, G_HLINE[0], C_YELLOW, C_BG);

    shrine_puts_centered(9,  l1, C_WHITE, C_BG);
    shrine_puts_centered(11, l2, C_WHITE, C_BG);

    if (with_choices) {
        shrine_puts_centered(17, "  A  SHOW MERCY  ",    C_LTGREEN,   C_BG);
        shrine_puts_centered(19, "  B  ISSUE JUDGMENT ", C_YELLOW,    C_BG);
        shrine_puts_centered(21, " DN  STONING       ",  C_LTRED,     C_BG);
    }
    shrine_puts_centered(TEXT_ROWS - 1,
                         "BOOT  RETURN TO MENU",
                         C_LTGRAY, C_BG);
}

static void scene_court(void)
{
    while (1) {
        // Generate a case.
        int a = (int)shrine_god(3);
        int c = (int)shrine_god(4);
        int v = (int)shrine_god(4);
        bool needs_victim = (c <= 1);   // murder / adultery need a victim
        bool again        = (shrine_god(5) == 0);

        char l1[64], l2[64];
        snprintf(l1, sizeof(l1), "%s %s", ACCUSED[a], CRIME[c]);
        if (needs_victim) {
            snprintf(l2, sizeof(l2), "%s%s", VICTIM[v],
                     again ? "  AGAIN." : "");
        } else {
            l2[0] = 0;
            if (again) snprintf(l2, sizeof(l2), "AGAIN.");
        }
        draw_court_frame(l1, l2, true);

        // Wait for verdict.
        int verdict = -1;
        while (verdict < 0) {
            shrine_input_scan();
            if (shrine_should_quit()) return;
            if (shrine_key_pressed(BTN_A))    verdict = 0;
            if (shrine_key_pressed(BTN_B))    verdict = 1;
            if (shrine_key_pressed(BTN_DOWN)) verdict = 2;
            shrine_sleep_ms(20);
        }

        // Feedback line.
        const char *result;
        color_t rc;
        switch (verdict) {
        default:
        case 0: result = "MERCY. THE CROWD MURMURS.";     rc = C_LTGREEN; break;
        case 1: result = "JUDGMENT. THE CROWD NODS.";     rc = C_YELLOW;  break;
        case 2: result = "STONES FLY. IT IS DONE.";       rc = C_LTRED;   break;
        }
        shrine_fill_rect(0, 16 * GLYPH_H, SCREEN_W, 8 * GLYPH_H,
                         PAL_RGB565[C_BG]);
        shrine_puts_centered(18, result, rc, C_BG);
        shrine_puts_centered(22, "A  NEXT CASE", C_LTGRAY, C_BG);
        shrine_beep(180 + verdict * 260, 120);

        // Wait for A (next case) or BOOT (quit).
        while (1) {
            shrine_input_scan();
            if (shrine_should_quit()) return;
            if (shrine_key_pressed(BTN_A)) break;
            shrine_sleep_ms(20);
        }
    }
}

// --- GodTalking scene ---
// Terry's DrawGodTalking draws a mountain sprite backdrop + a burning
// bush + a shower of random-length flame lines around the flame area,
// then his UpTheMountain flow calls GodWord in a 16-iteration loop
// (or GodBiblePassage once). We reproduce the same feel with
// procedural art (mountain silhouette from lines, bush trunk + flame
// pixels) and reuse our vocab list for the oracle text.

static void draw_god_backdrop(uint32_t t_ms)
{
    shrine_clear(C_LTCYAN);   // sky
    // Ground band at bottom
    shrine_fill_rect(0, SCREEN_H - 60, SCREEN_W, 60, C_BROWN);
    // Mountain ridges — zig-zag from screen center, decreasing width.
    // Same pattern as Terry's Mountain: successive line segments
    // stepping upward and folding back, forming a jagged peak.
    int cx = SCREEN_W / 2;
    int cy = SCREEN_H - 60;
    int steps[]  = { 60, -55, 45, -40, 35, -30, 22, -14, 0 };
    int drops[]  = { 12,  10,  9,  8,  7,  6,  5,  4, 0 };
    int lx = cx, ly = cy;
    for (unsigned i = 0; i < sizeof(steps)/sizeof(steps[0]) - 1; i++) {
        int nx = cx + steps[i];
        int ny = ly - drops[i];
        shrine_line(lx, ly, nx, ny, C_BROWN);
        shrine_line(lx, ly - 1, nx, ny - 1, C_BROWN);
        lx = nx; ly = ny;
    }
    // Sun (upper left)
    shrine_fill_circle(30, 24, 12, C_YELLOW);
    shrine_circle(30, 24, 12, C_BROWN);

    // Blinking "MT HOREB" title
    if ((t_ms / 500) & 1) {
        shrine_puts_centered(9, "MT  HOREB", C_BLACK, C_LTCYAN);
    }

    // Burning bush: small brown trunk at right side
    int bx = SCREEN_W - 60, by = SCREEN_H - 70;
    shrine_fill_rect(bx - 2, by, 4, 14, C_BROWN);
    // Flame flicker: cluster of yellow/red pixels dancing above trunk
    for (int i = 0; i < 20; i++) {
        int dx = (int)shrine_god(20) - 10;
        int dy = (int)shrine_god(18);
        color_t c = (i & 1) ? C_YELLOW : C_LTRED;
        shrine_pixel(bx + dx, by - dy, c);
    }
    // Small yellow crown atop trunk
    shrine_fill_circle(bx, by - 4, 4, C_YELLOW);
}

static void scene_god(void)
{
    const char *saying = NULL;
    bool passage_mode  = false;
    uint32_t frame_ms  = shrine_ms();

    draw_god_backdrop(frame_ms);
    shrine_puts_centered(20, "PRESS A  A WORD",     C_LTGREEN, C_LTCYAN);
    shrine_puts_centered(22, "PRESS B  A PASSAGE",  C_YELLOW,  C_LTCYAN);

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        bool need_redraw = false;

        if (shrine_key_pressed(BTN_A)) {
            saying = VOCAB[shrine_god(VOCAB_N)];
            passage_mode = false;
            need_redraw = true;
            shrine_beep(1600, 60);
            shrine_beep(2200, 80);
        }
        if (shrine_key_pressed(BTN_B)) {
            passage_mode = true;
            need_redraw = true;
            shrine_beep(1000, 60);
            shrine_beep(1400, 60);
            shrine_beep(1800, 100);
        }

        // Bush flame reflicker every ~250 ms even without input.
        uint32_t now = shrine_ms();
        if (now - frame_ms > 250) {
            frame_ms = now;
            need_redraw = true;
        }

        if (need_redraw) {
            draw_god_backdrop(now);
            if (saying && !passage_mode) {
                shrine_puts_centered(19, "GOD SAYS", C_LTGREEN, C_LTCYAN);
                // Big text: use scaled render for the word.
                int scale = 2;
                int w = 8 * scale * (int)strlen(saying);
                if (w > SCREEN_W - 20) scale = 1;
                shrine_puts_centered_scaled(22 * GLYPH_H, saying,
                                            C_BLACK, C_LTCYAN, scale);
                shrine_puts_centered(26, "A AGAIN   B PASSAGE   BOOT EXIT",
                                     C_LTGRAY, C_LTCYAN);
            } else if (passage_mode) {
                // Random-line "passage" — three lines drawn from vocab
                // words assembled into short phrases. Original wording.
                const char *w1 = VOCAB[shrine_god(VOCAB_N)];
                const char *w2 = VOCAB[shrine_god(VOCAB_N)];
                const char *w3 = VOCAB[shrine_god(VOCAB_N)];
                char l1[48], l2[48], l3[48];
                snprintf(l1, sizeof(l1), "AND HE LIFTED UP HIS %s,", w1);
                snprintf(l2, sizeof(l2), "AND THE %s WAS UPON HIM,",   w2);
                snprintf(l3, sizeof(l3), "AND HE CALLED IT %s.",       w3);
                shrine_puts_centered(19, l1, C_BLACK, C_LTCYAN);
                shrine_puts_centered(21, l2, C_BLACK, C_LTCYAN);
                shrine_puts_centered(23, l3, C_BLACK, C_LTCYAN);
                shrine_puts_centered(26, "A WORD   B PASSAGE   BOOT EXIT",
                                     C_LTGRAY, C_LTCYAN);
            } else {
                shrine_puts_centered(20, "PRESS A  A WORD",
                                     C_LTGREEN, C_LTCYAN);
                shrine_puts_centered(22, "PRESS B  A PASSAGE",
                                     C_YELLOW,  C_LTCYAN);
            }
        }
        shrine_sleep_ms(30);
    }
}

// --- Stub scenes (flesh out one at a time in follow-up commits) ---
static void scene_stub(const char *title, const char *evocative)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  AFTER EGYPT  *", C_YELLOW, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 3, G_HLINE[0], C_YELLOW, C_BG);
    shrine_puts_centered(10, title,     C_LTCYAN, C_BG);
    shrine_puts_centered(14, evocative, C_WHITE,  C_BG);
    shrine_puts_centered(20, "coming soon.", C_LTGRAY, C_BG);
    shrine_puts_centered(TEXT_ROWS - 1,
                         "A OR BOOT  RETURN",
                         C_LTGRAY, C_BG);
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;
        shrine_sleep_ms(30);
    }
}

// --- Entry point ---
void game_afteregypt_run(void)
{
    s_sel = 0;
    draw_menu();
    // Boot chime.
    shrine_beep(392, 100);
    shrine_beep(523, 100);
    shrine_beep(659, 160);

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (shrine_key_pressed(BTN_UP)) {
            s_sel = (s_sel - 1 + N_MENU) % N_MENU;
            draw_menu();
            shrine_beep(1200, 20);
        }
        if (shrine_key_pressed(BTN_DOWN)) {
            s_sel = (s_sel + 1) % N_MENU;
            draw_menu();
            shrine_beep(1200, 20);
        }
        if (shrine_key_pressed(BTN_A)) {
            shrine_beep(1800, 60);
            switch (MENU[s_sel].id) {
            case SC_COURT:  scene_court(); break;
            case SC_GOD:    scene_god();   break;
            case SC_CLOUDS: scene_stub("VIEW CLOUDS",   "SIGNS IN THE SKY");       break;
            case SC_MAP:    scene_stub("VIEW MAP",      "THE WILDERNESS BEFORE YOU"); break;
            case SC_CAMP:   scene_stub("BREAK CAMP",    "THE PEOPLE PREPARE");     break;
            case SC_WATER:  scene_stub("WATER ROCK",    "STRIKE THE ROCK");        break;
            case SC_BATTLE: scene_stub("BATTLE",        "SWORDS ARE DRAWN");       break;
            case SC_QUAIL:  scene_stub("QUAIL",         "MEAT FALLS FROM HEAVEN"); break;
            case SC_HOREB:  scene_stub("MT HOREB",      "THE MOUNTAIN OF GOD");    break;
            default: break;
            }
            draw_menu();
        }
        shrine_sleep_ms(30);
    }
}
