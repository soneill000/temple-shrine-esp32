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
#include "display.h"
#include "templeshim.h"
#include "bible.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// --- Off-screen framebuffer, shared with other scenes (see scene_fb.h).
// Composed in memory then pushed as ONE SPI transaction via
// display_present_full — eliminates the mid-frame clear/redraw flicker
// that the direct shrine_* pipeline produces on the ILI9341 (no vsync).
#include "scene_fb.h"
#define s_fb g_scene_fb

static inline uint16_t rgb(color_t c) { return PAL_RGB565[c & 15]; }

static inline void fb_pixel(int x, int y, color_t c)
{
    if ((unsigned)x < (unsigned)SCREEN_W && (unsigned)y < (unsigned)SCREEN_H)
        s_fb[y * SCREEN_W + x] = rgb(c);
}
static inline void fb_pixel_raw(int x, int y, uint16_t r)
{
    if ((unsigned)x < (unsigned)SCREEN_W && (unsigned)y < (unsigned)SCREEN_H)
        s_fb[y * SCREEN_W + x] = r;
}
static void fb_fill_rect(int x, int y, int w, int h, color_t c)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    uint16_t v = rgb(c);
    for (int j = 0; j < h; j++) {
        uint16_t *p = &s_fb[(y + j) * SCREEN_W + x];
        for (int i = 0; i < w; i++) p[i] = v;
    }
}
static inline void fb_hline(int x, int y, int w, color_t c) { fb_fill_rect(x, y, w, 1, c); }
static inline void fb_vline(int x, int y, int h, color_t c) { fb_fill_rect(x, y, 1, h, c); }
static void fb_rect(int x, int y, int w, int h, color_t c)
{
    fb_hline(x,         y,         w, c);
    fb_hline(x,         y + h - 1, w, c);
    fb_vline(x,         y,         h, c);
    fb_vline(x + w - 1, y,         h, c);
}
static void fb_line(int x0, int y0, int x1, int y1, color_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void fb_fill_circle(int cx, int cy, int r, color_t c)
{
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        fb_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
static void fb_circle(int cx, int cy, int r, color_t c)
{
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        fb_pixel(cx + x, cy + y, c); fb_pixel(cx - x, cy + y, c);
        fb_pixel(cx + x, cy - y, c); fb_pixel(cx - x, cy - y, c);
        fb_pixel(cx + y, cy + x, c); fb_pixel(cx - y, cy + x, c);
        fb_pixel(cx + y, cy - x, c); fb_pixel(cx - y, cy - x, c);
        x++;
        if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6;
    }
}
static void fb_putcxy(int x, int y, char ch, color_t fg, color_t bg)
{
    uint8_t code = (uint8_t)ch;
    if (code >= 128) code = ' ';
    const uint8_t *g = FONT8X8[code];
    uint16_t f = rgb(fg), b = rgb(bg);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            fb_pixel_raw(x + col, y + row, (bits & (1 << col)) ? f : b);
        }
    }
}
static void fb_puts(int col, int row, const char *s, color_t fg, color_t bg)
{
    int x = col * GLYPH_W, y = row * GLYPH_H;
    while (*s) { fb_putcxy(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static void fb_puts_centered(int row, const char *s, color_t fg, color_t bg)
{
    int n = 0; while (s[n]) n++;
    int x = (SCREEN_W - n * GLYPH_W) / 2;
    int y = row * GLYPH_H;
    while (*s) { fb_putcxy(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static inline void fb_clear(color_t c)
{
    uint16_t v = rgb(c);
    int n = SCREEN_W * SCREEN_H;
    for (int i = 0; i < n; i++) s_fb[i] = v;
}
static inline void fb_present(void) { display_present_full(s_fb); }

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

// Draw the god-talking backdrop straight into the framebuffer. Called
// once per frame; single fb_present() at end blits the whole thing.
static void fb_god_backdrop(uint32_t t_ms)
{
    fb_clear(C_LTCYAN);   // sky
    fb_fill_rect(0, SCREEN_H - 60, SCREEN_W, 60, C_BROWN);
    // Mountain ridge zig-zag
    int cx = SCREEN_W / 2;
    int cy = SCREEN_H - 60;
    int steps[] = { 60, -55, 45, -40, 35, -30, 22, -14, 0 };
    int drops[] = { 12,  10,  9,  8,  7,  6,  5,  4, 0 };
    int lx = cx, ly = cy;
    for (unsigned i = 0; i < sizeof(steps)/sizeof(steps[0]) - 1; i++) {
        int nx = cx + steps[i];
        int ny = ly - drops[i];
        fb_line(lx, ly, nx, ny, C_BROWN);
        fb_line(lx, ly - 1, nx, ny - 1, C_BROWN);
        lx = nx; ly = ny;
    }
    fb_fill_circle(30, 24, 12, C_YELLOW);
    fb_circle    (30, 24, 12, C_BROWN);

    if ((t_ms / 500) & 1)
        fb_puts_centered(9, "MT  HOREB", C_BLACK, C_LTCYAN);

    // Burning bush trunk + flame flicker
    int bx = SCREEN_W - 60, by = SCREEN_H - 70;
    fb_fill_rect(bx - 2, by, 4, 14, C_BROWN);
    for (int i = 0; i < 20; i++) {
        int dx = (int)shrine_god(20) - 10;
        int dy = (int)shrine_god(18);
        fb_pixel(bx + dx, by - dy, (i & 1) ? C_YELLOW : C_LTRED);
    }
    fb_fill_circle(bx, by - 4, 4, C_YELLOW);
}

static void scene_god(void)
{
    const char *saying = NULL;
    bool passage_mode  = false;
    const char *l1 = NULL, *l2 = NULL, *l3 = NULL;
    char pbuf1[48], pbuf2[48], pbuf3[48];

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        uint32_t now = shrine_ms();

        if (shrine_key_pressed(BTN_A)) {
            saying = VOCAB[shrine_god(VOCAB_N)];
            passage_mode = false;
            shrine_beep(1600, 60);
            shrine_beep(2200, 80);
        }
        if (shrine_key_pressed(BTN_B)) {
            const char *w1 = VOCAB[shrine_god(VOCAB_N)];
            const char *w2 = VOCAB[shrine_god(VOCAB_N)];
            const char *w3 = VOCAB[shrine_god(VOCAB_N)];
            snprintf(pbuf1, sizeof(pbuf1), "AND HE LIFTED UP HIS %s,", w1);
            snprintf(pbuf2, sizeof(pbuf2), "AND THE %s WAS UPON HIM,",   w2);
            snprintf(pbuf3, sizeof(pbuf3), "AND HE CALLED IT %s.",       w3);
            l1 = pbuf1; l2 = pbuf2; l3 = pbuf3;
            passage_mode = true;
            shrine_beep(1000, 60);
            shrine_beep(1400, 60);
            shrine_beep(1800, 100);
        }

        // Draw whole frame off-screen, then present.
        fb_god_backdrop(now);
        if (saying && !passage_mode) {
            fb_puts_centered(19, "GOD SAYS", C_LTGREEN, C_LTCYAN);
            // Big text via manual scaled putc.
            int scale = 2;
            int nlen = 0; while (saying[nlen]) nlen++;
            if (nlen * 8 * scale > SCREEN_W - 20) scale = 1;
            int text_w = nlen * 8 * scale;
            int sx = (SCREEN_W - text_w) / 2;
            int sy = 22 * GLYPH_H;
            for (int i = 0; i < nlen; i++) {
                uint8_t code = (uint8_t)saying[i]; if (code >= 128) code = ' ';
                const uint8_t *g = FONT8X8[code];
                for (int r = 0; r < 8; r++) {
                    uint8_t bits = g[r];
                    for (int col = 0; col < 8; col++) {
                        if (bits & (1 << col)) {
                            fb_fill_rect(sx + i * 8 * scale + col * scale,
                                         sy + r * scale, scale, scale, C_BLACK);
                        }
                    }
                }
            }
            fb_puts_centered(26, "A AGAIN   B PASSAGE   BOOT EXIT",
                             C_LTGRAY, C_LTCYAN);
        } else if (passage_mode) {
            fb_puts_centered(19, l1, C_BLACK, C_LTCYAN);
            fb_puts_centered(21, l2, C_BLACK, C_LTCYAN);
            fb_puts_centered(23, l3, C_BLACK, C_LTCYAN);
            fb_puts_centered(26, "A WORD   B PASSAGE   BOOT EXIT",
                             C_LTGRAY, C_LTCYAN);
        } else {
            fb_puts_centered(20, "PRESS A  A WORD",    C_LTGREEN, C_LTCYAN);
            fb_puts_centered(22, "PRESS B  A PASSAGE", C_YELLOW,  C_LTCYAN);
        }
        fb_present();
        shrine_sleep_ms(60);
    }
}

// --- Water Rock scene ---
// Terry's WaterRock: Moses strikes the rock with a rod, water gushes.
// 5 sprites, 4-frame animation. Our version: procedural rock + growing
// water plume + strike counter. Fill the pot to move on.
static void scene_water(void)
{
    int strikes = 0;
    const int NEEDED = 8;
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        bool struck = shrine_key_pressed(BTN_A);
        if (struck) {
            strikes++;
            shrine_beep(600, 40);
            shrine_beep(400, 60);
        }

        fb_clear(C_BG);
        fb_puts_centered(1, "*  WATER ROCK  *", C_YELLOW, C_BG);
        fb_puts_centered(3, "STRIKE THE ROCK",   C_LTCYAN, C_BG);
        fb_hline(GLYPH_W, 5 * GLYPH_H, SCREEN_W - 2 * GLYPH_W, C_YELLOW);

        int rx = SCREEN_W - 100, ry = 120;
        for (int r = 0; r < 60; r += 4) {
            int w = 50 - r / 4;
            fb_fill_rect(rx - w / 2, ry + r, w, 4, C_DKGRAY);
        }
        fb_rect(rx - 26, ry, 52, 60, C_LTGRAY);

        int mx = 60, my = 130;
        fb_fill_rect(mx, my, 4, 40, C_BROWN);
        fb_fill_circle(mx + 2, my - 6, 6, C_LTGRAY);
        int rod_len = 34 + (struck ? 6 : 0);
        fb_line(mx + 4, my + 8, mx + rod_len, my - 4, C_YELLOW);
        fb_line(mx + 4, my + 9, mx + rod_len, my - 3, C_YELLOW);

        int level = strikes * 6; if (level > 90) level = 90;
        for (int i = 0; i < level; i++) {
            int px = rx - 40 - (int)shrine_god(20);
            int py = ry + 20 - i;
            color_t c = (i & 1) ? C_LTCYAN : C_LTBLUE;
            fb_pixel(px,     py, c);
            fb_pixel(px + 1, py, c);
        }
        if (strikes > 0) {
            int pw = strikes * 8; if (pw > 160) pw = 160;
            fb_fill_rect(rx - 60 - pw / 2, SCREEN_H - 40, pw, 4, C_LTBLUE);
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "STRIKES: %d / %d", strikes, NEEDED);
        fb_puts_centered(TEXT_ROWS - 3, buf, C_WHITE, C_BG);
        fb_puts_centered(TEXT_ROWS - 1, "A STRIKE   BOOT EXIT",
                         C_LTGRAY, C_BG);

        if (strikes >= NEEDED) {
            fb_puts_centered(15, "THE PEOPLE DRINK.", C_LTGREEN, C_BG);
            fb_puts_centered(17, "MOSES REJOICES.",  C_YELLOW,  C_BG);
            fb_present();
            shrine_beep(1800, 100);
            shrine_beep(2200, 100);
            shrine_beep(2600, 200);
            while (1) {
                shrine_input_scan();
                if (shrine_should_quit()) return;
                if (shrine_key_pressed(BTN_A)) return;
                shrine_sleep_ms(30);
            }
        }
        fb_present();
        shrine_sleep_ms(50);
    }
}

// --- Battle scene ---
// Terry's Battle: HACK_PERIOD (0.25s) rhythm combat with held_up state.
// Our version: two lines of warriors, press A to attack on the beat,
// scores casualties. Simple rhythm-timing mini-game.
static void scene_battle(void)
{
    int ally = 12, enemy = 12;
    int score_hit = 0, score_miss = 0;
    uint32_t last_beat = shrine_ms();
    uint32_t beat_period = 700;   // ms
    bool  we_swing = false;
    uint32_t swing_end = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        uint32_t now = shrine_ms();

        if (shrine_key_pressed(BTN_A) && ally > 0 && enemy > 0) {
            // Timing: hit if within +/- 150ms of beat
            uint32_t since = now - last_beat;
            uint32_t until = beat_period - since;
            uint32_t off = since < until ? since : until;
            if (off < 150) {
                enemy--;
                score_hit++;
                shrine_beep(1800, 40);
                shrine_beep(2200, 60);
            } else {
                score_miss++;
                shrine_beep(400, 60);
            }
            we_swing = true;
            swing_end = now + 200;
        }

        // Beat advances; every 3 beats, enemy strikes back
        if (now - last_beat > beat_period) {
            last_beat = now;
            static int beat_cnt = 0;
            beat_cnt++;
            if ((beat_cnt % 3 == 0) && ally > 0 && enemy > 0) {
                ally--;
                shrine_beep(300, 80);
            }
        }
        if (now > swing_end) we_swing = false;

        shrine_clear(C_BG);
        shrine_puts_centered(1, "*  BATTLE  *", C_YELLOW, C_BG);
        shrine_puts_centered(3, "TIME YOUR STRIKES", C_LTCYAN, C_BG);
        for (int c = 1; c < TEXT_COLS - 1; c++)
            shrine_putc(c, 5, G_HLINE[0], C_YELLOW, C_BG);

        // Ally line (left) — small figures
        for (int i = 0; i < ally; i++) {
            int fx = 30 + i * 12;
            shrine_fill_rect(fx, 100, 3, 18, C_LTGREEN);
            shrine_fill_circle(fx + 1, 96, 3, C_LTGRAY);
            // Sword tick when swinging
            if (we_swing) shrine_line(fx + 3, 108, fx + 10, 100, C_YELLOW);
        }
        // Enemy line (right)
        for (int i = 0; i < enemy; i++) {
            int fx = SCREEN_W - 42 - i * 12;
            shrine_fill_rect(fx, 100, 3, 18, C_LTRED);
            shrine_fill_circle(fx + 1, 96, 3, C_DKGRAY);
        }
        // Beat indicator
        uint32_t beat_pct = ((now - last_beat) * 100) / beat_period;
        if (beat_pct > 100) beat_pct = 100;
        int bar = (int)((100 - beat_pct) * SCREEN_W / 100);
        shrine_fill_rect(0, 130, bar, 2, C_YELLOW);

        char buf[48];
        snprintf(buf, sizeof(buf), "ALLIES %2d   ENEMY %2d", ally, enemy);
        shrine_puts_centered(20, buf, C_WHITE, C_BG);
        snprintf(buf, sizeof(buf), "STRIKES  HIT %d   MISS %d", score_hit, score_miss);
        shrine_puts_centered(22, buf, C_LTGRAY, C_BG);
        shrine_puts_centered(TEXT_ROWS - 1, "A STRIKE   BOOT EXIT",
                             C_LTGRAY, C_BG);

        if (enemy == 0) {
            shrine_puts_centered(15, "THE FIELD IS OURS.", C_LTGREEN, C_BG);
            shrine_beep(2000, 100); shrine_beep(2400, 100); shrine_beep(2800, 200);
            while (1) {
                shrine_input_scan();
                if (shrine_should_quit()) return;
                if (shrine_key_pressed(BTN_A)) return;
                shrine_sleep_ms(30);
            }
        }
        if (ally == 0) {
            shrine_puts_centered(15, "WE ARE ROUTED.", C_LTRED, C_BG);
            shrine_beep(300, 200); shrine_beep(200, 300); shrine_beep(150, 400);
            while (1) {
                shrine_input_scan();
                if (shrine_should_quit()) return;
                if (shrine_key_pressed(BTN_A)) return;
                shrine_sleep_ms(30);
            }
        }
        shrine_sleep_ms(30);
    }
}

// --- Quail scene ---
// Terry's Quail: 128 quail flock sim with alive/dead states. Our
// version: sky full of falling quail dots; player moves a small basket
// with LEFT/RIGHT to catch them. 30-second round.
#define QUAIL_N 60
typedef struct { int x, y; int8_t vy; bool alive; } quail_t;
static quail_t s_quail[QUAIL_N];

static void scene_quail(void)
{
    for (int i = 0; i < QUAIL_N; i++) {
        s_quail[i].x = (int)shrine_god(SCREEN_W - 8) + 4;
        s_quail[i].y = -(int)shrine_god(SCREEN_H);
        s_quail[i].vy = 1 + (int)shrine_god(3);
        s_quail[i].alive = true;
    }
    int basket_x = SCREEN_W / 2;
    int caught = 0;
    uint32_t start = shrine_ms();
    const uint32_t DUR_MS = 30000;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        uint32_t now = shrine_ms();
        int elapsed = (int)((now - start) / 1000);
        if (elapsed >= (int)(DUR_MS / 1000)) break;

        if (shrine_key_held(BTN_LEFT))  basket_x -= 3;
        if (shrine_key_held(BTN_RIGHT)) basket_x += 3;
        if (basket_x < 14) basket_x = 14;
        if (basket_x > SCREEN_W - 14) basket_x = SCREEN_W - 14;

        // Fall + catch check
        for (int i = 0; i < QUAIL_N; i++) {
            if (!s_quail[i].alive) continue;
            s_quail[i].y += s_quail[i].vy;
            if (s_quail[i].y >= SCREEN_H - 20) {
                // Basket catches within +/- 12
                if (s_quail[i].x > basket_x - 12 && s_quail[i].x < basket_x + 12
                    && s_quail[i].y < SCREEN_H - 10) {
                    caught++;
                    shrine_beep(1600 + caught * 20, 20);
                }
                // Respawn top
                s_quail[i].x = (int)shrine_god(SCREEN_W - 8) + 4;
                s_quail[i].y = -(int)shrine_god(40);
                s_quail[i].vy = 1 + (int)shrine_god(3);
            }
        }

        shrine_clear(C_BG);
        shrine_puts_centered(1, "*  QUAIL  *", C_YELLOW, C_BG);
        shrine_puts_centered(3, "MEAT FALLS FROM HEAVEN", C_LTCYAN, C_BG);

        // Draw quail as small yellow blobs
        for (int i = 0; i < QUAIL_N; i++) {
            if (!s_quail[i].alive) continue;
            if (s_quail[i].y < 30 || s_quail[i].y > SCREEN_H) continue;
            shrine_fill_rect(s_quail[i].x - 1, s_quail[i].y, 3, 2, C_YELLOW);
            shrine_pixel(s_quail[i].x, s_quail[i].y - 1, C_BROWN);
        }
        // Ground line
        shrine_hline(0, SCREEN_H - 20, SCREEN_W, C_BROWN);
        // Basket
        shrine_fill_rect(basket_x - 12, SCREEN_H - 20, 24, 10, C_BROWN);
        shrine_hline(basket_x - 12, SCREEN_H - 22, 24, C_YELLOW);

        char buf[32];
        snprintf(buf, sizeof(buf), "CAUGHT %d   T-%02d", caught,
                 (int)(DUR_MS / 1000) - elapsed);
        shrine_puts_centered(TEXT_ROWS - 1, buf, C_WHITE, C_BG);
        shrine_sleep_ms(30);
    }

    // Round end
    shrine_clear(C_BG);
    shrine_puts_centered(10, "* THE PEOPLE ARE FED *", C_LTGREEN, C_BG);
    char buf[32];
    snprintf(buf, sizeof(buf), "CAUGHT %d QUAIL", caught);
    shrine_puts_centered(13, buf, C_WHITE, C_BG);
    shrine_puts_centered(20, "A  RETURN", C_LTGRAY, C_BG);
    shrine_beep(2000, 100); shrine_beep(2400, 100); shrine_beep(2800, 200);
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;
        shrine_sleep_ms(30);
    }
}

// --- View Clouds scene ---
// Direct-port from Terry's Clouds.HC using the new TempleShim (CDC +
// Gr* API). Terry's original uses multi-processor rendering across
// mp_cnt cores; we run single-core sequentially. Terry's 16 clouds x
// 512 dots become 8 clouds x 128 dots here to fit RAM comfortably.
// Coordinates below are in Terry's 640x480 space — gr_dc.scale=2
// takes care of the /2 on write into our 320x240 fb.

#define AE_CLOUDS_NUM     8
#define AE_CLOUD_PTS      128
#define AE_CLOUD_PENS     8
#define AE_CLOUD_PEN_PTS  12
#define AE_CLOUD_PEN_SIZE 12
#define AE_SKY_HEIGHT     (30 * 8)     // Terry uses SKY_LINES * FONT_HEIGHT

typedef struct {
    float    x, y, dx, dy;
    int      w, h;
    uint16_t color;
    int16_t  px[AE_CLOUD_PTS];
    int16_t  py[AE_CLOUD_PTS];
    uint16_t pc[AE_CLOUD_PTS];
} ae_cloud_t;

typedef struct {
    int8_t px[AE_CLOUD_PEN_PTS];
    int8_t py[AE_CLOUD_PEN_PTS];
} ae_cloud_pen_t;

static ae_cloud_t     s_clouds[AE_CLOUDS_NUM];
static ae_cloud_pen_t s_pens[AE_CLOUD_PENS];

// Cheap Gaussian-ish sample: sum of 6 signed uniform samples,
// matching Terry's SAMPLES=6 in Init().
static int16_t gauss6(int scale_i16)
{
    int32_t k = 0;
    for (int j = 0; j < 6; j++) k += (int32_t)((int16_t)shrine_god(65536));
    return (int16_t)((k * scale_i16) / (32767 * 6));
}

static void clouds_init(void)
{
    int W = 640, H = AE_SKY_HEIGHT;   // Terry-space dimensions
    for (int i = 0; i < AE_CLOUDS_NUM; i++) {
        ae_cloud_t *c = &s_clouds[i];
        float r1 = ((float)shrine_god(1000) / 500.0f) - 1.0f;   // -1..+1
        float r2 = ((float)shrine_god(1000) / 500.0f) - 1.0f;
        c->x  = W / 2.0f + r1 * W / 4.0f;
        c->y  = H / 2.0f + r2 * H / 4.0f;
        c->dx = ((float)shrine_god(1000) / 500.0f) - 1.0f;   // ~ Terry's RandI32 scaled
        c->dy = ((float)shrine_god(1000) / 500.0f) - 1.0f;
        c->w  = 100; c->h = 50;
        c->color = (uint16_t)shrine_god(65536);
        for (int l = 0; l < AE_CLOUD_PTS; l++) {
            c->px[l] = gauss6(c->w);
            c->py[l] = gauss6(c->h);
            c->pc[l] = (uint16_t)shrine_god(65536);
        }
    }
    for (int i = 0; i < AE_CLOUD_PENS; i++) {
        for (int j = 0; j < AE_CLOUD_PEN_PTS; j++) {
            s_pens[i].px[j] = (int8_t)shrine_god(AE_CLOUD_PEN_SIZE);
            s_pens[i].py[j] = (int8_t)shrine_god(AE_CLOUD_PEN_SIZE);
        }
    }
}

// Terry's AnimateTask body (single-tick): jitter pen dots, drift clouds.
static void clouds_animate(void)
{
    for (int i = 0; i < AE_CLOUD_PENS; i++) {
        ae_cloud_pen_t *p = &s_pens[i];
        for (int j = 0; j < AE_CLOUD_PEN_PTS; j++) {
            int nx = p->px[j] + (int)shrine_god(3) - 1;
            int ny = p->py[j] + (int)shrine_god(3) - 1;
            if (nx < 0) nx = 0;
            if (nx >= AE_CLOUD_PEN_SIZE) nx = AE_CLOUD_PEN_SIZE - 1;
            if (ny < 0) ny = 0;
            if (ny >= AE_CLOUD_PEN_SIZE) ny = AE_CLOUD_PEN_SIZE - 1;
            p->px[j] = (int8_t)nx; p->py[j] = (int8_t)ny;
        }
    }
    for (int i = 0; i < AE_CLOUDS_NUM; i++) {
        ae_cloud_t *c = &s_clouds[i];
        c->x += c->dx;
        c->y += c->dy;
        float ylo = 0, yhi = 0.7f * AE_SKY_HEIGHT;
        if (c->y < ylo) c->y = ylo;
        if (c->y > yhi) c->y = yhi;
        // Wrap x horizontally
        if (c->x < -200) c->x = 640 + 100;
        if (c->x > 640 + 200) c->x = -100;
        c->color = (uint16_t)(65535.0f * c->y / (0.8f * AE_SKY_HEIGHT));
    }
}

// Terry's DrawIt body: for each cloud, for each dot, blot a pen.
static void clouds_drawit(void)
{
    for (int j = 0; j < AE_CLOUDS_NUM; j++) {
        ae_cloud_t *c = &s_clouds[j];
        for (int i = 0; i < AE_CLOUD_PTS; i++) {
            uint16_t k = c->pc[i];
            gr_dc.color = (k < c->color) ? C_LTGRAY : C_WHITE;

            int xx = (int)c->x + c->px[i];
            int yy = (int)c->y + c->py[i];

            // Jitter dot ±16, with occasional pull-back-to-center.
            int kx = (int)shrine_god(32) - 16;
            if (kx == -16) kx = -c->px[i];
            c->px[i] += (kx > 0) - (kx < 0);
            int ky = (int)shrine_god(32) - 16;
            if (ky == -16) ky = -c->py[i];
            c->py[i] += (ky > 0) - (ky < 0);

            // GrBlot substitute: stamp the current pen's dots at (xx, yy).
            ae_cloud_pen_t *p = &s_pens[i & (AE_CLOUD_PENS - 1)];
            for (int q = 0; q < AE_CLOUD_PEN_PTS; q++) {
                GrPlot(&gr_dc, xx + p->px[q], yy + p->py[q]);
            }
        }
    }
}

// Procedural mountain silhouette placed where Terry's Mountain.HC
// sprite would be drawn — at (0, SKY_LINES*FONT_HEIGHT) with the
// shape extending upward. Terry's actual sprite is a BP-referenced
// bitmap in Mountain.HC.Z that we haven't extracted yet; this stands
// in for it until we do.
static void draw_mountain_placeholder(int sky_bottom_terry)
{
    // Filled triangular peak centered on screen, sitting on the sky/
    // yellow boundary and pointing up into the sky area.
    int cx = 320;                          // Terry-space center x
    int base_y = sky_bottom_terry;         // rests on the boundary
    int peak_y = sky_bottom_terry - 180;   // apex 180 px up
    int base_half = 260;                   // wide base
    // Filled body: horizontal slices from apex to base, widening
    int slices = base_y - peak_y;
    gr_dc.color = C_BROWN;
    for (int i = 0; i < slices; i++) {
        int hw = base_half * i / slices;
        GrFillRect(&gr_dc, cx - hw, peak_y + i, hw * 2, 1);
    }
    // Ridge outline in DK color so the mountain reads as 3D
    gr_dc.color = C_DKGRAY;
    gr_dc.thick = 2;
    GrLine(&gr_dc, cx - base_half, base_y, cx,             peak_y);
    GrLine(&gr_dc, cx,             peak_y, cx + base_half, base_y);
    gr_dc.thick = 1;
    // A second smaller peak to the left for visual interest
    int cx2 = 90;
    int base2_y = sky_bottom_terry;
    int peak2_y = sky_bottom_terry - 110;
    int bh2 = 130;
    for (int i = 0; i < (base2_y - peak2_y); i++) {
        int hw = bh2 * i / (base2_y - peak2_y);
        gr_dc.color = C_BROWN;
        GrFillRect(&gr_dc, cx2 - hw, peak2_y + i, hw * 2, 1);
    }
    gr_dc.color = C_DKGRAY;
    gr_dc.thick = 2;
    GrLine(&gr_dc, cx2 - bh2, base2_y, cx2,       peak2_y);
    GrLine(&gr_dc, cx2,       peak2_y, cx2 + bh2, base2_y);
    gr_dc.thick = 1;
}

static void scene_clouds(void)
{
    // Terry's Clouds.HC uses SKY_LINES=30 rows of 8-pixel text as the
    // sky region, then 5 rows of yellow strip, then the Bible verse
    // occupies the rest of the doc. Layout in Terry-space (640x480):
    //     y=0..240   LTCYAN sky
    //     y=240..280 YELLOW strip (5*8 rows)
    //     y=280..480 verse text on the default BLUE document background
    const int sky_h    = 30 * 8;   // Terry: SKY_LINES * FONT_HEIGHT
    const int yellow_h = 5  * 8;

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);
    clouds_init();

    uint32_t last = shrine_ms();
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;

        uint32_t now = shrine_ms();
        if (now - last > 20) { clouds_animate(); last = now; }

        // Region backgrounds — mirror Terry's "$$BG,COLOR$$%h*c" text.
        gr_dc.color = C_LTCYAN;
        GrFillRect(&gr_dc, 0, 0,                  640, sky_h);
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, sky_h,              640, yellow_h);
        gr_dc.color = C_BG;   // Terry's document default is BLUE bg
        GrFillRect(&gr_dc, 0, sky_h + yellow_h,   640, 480 - sky_h - yellow_h);

        // Mountain — Terry's Sprite3(dc, 0, SKY_LINES*FONT_HEIGHT, ...).
        draw_mountain_placeholder(sky_h);

        // Clouds — Terry's DrawIt calls MPDrawClouds workers.
        clouds_drawit();

        // Bible verse region — Terry calls BibleVerse(,"Exodus,14:19",7).
        // Look up the same citation in our curated table; kjv_lookup
        // returns NULL if we don't have the entry, in which case we
        // just show the citation.
        const kjv_verse_t *kv = kjv_lookup("EXODUS", 14, 19);
        gr_dc.color = C_YELLOW;
        if (kv) {
            GrPrint(&gr_dc, 40, sky_h + yellow_h + 24, kv->text);
        }
        gr_dc.color = C_LTGREEN;
        GrPrint(&gr_dc, 40, sky_h + yellow_h + 60, "-- EXODUS 14:19");

        // Chrome
        gr_dc.color = C_BLACK;
        GrPrint(&gr_dc, 8, sky_h + 8, "VIEW CLOUDS");
        gr_dc.color = C_LTGRAY;
        GrPrint(&gr_dc, 8, sky_h + yellow_h + 108,
                "PRESS A OR BOOT TO RETURN");

        DCPresent(&gr_dc);
        shrine_sleep_ms(40);
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
            case SC_CLOUDS: scene_clouds(); break;
            case SC_MAP:    scene_stub("VIEW MAP",      "THE WILDERNESS BEFORE YOU"); break;
            case SC_CAMP:   scene_stub("BREAK CAMP",    "THE PEOPLE PREPARE");     break;
            case SC_WATER:  scene_water();  break;
            case SC_BATTLE: scene_battle(); break;
            case SC_QUAIL:  scene_quail();  break;
            case SC_HOREB:  scene_stub("MT HOREB",      "THE MOUNTAIN OF GOD");    break;
            default: break;
            }
            draw_menu();
        }
        shrine_sleep_ms(30);
    }
}
