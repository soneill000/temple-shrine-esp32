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

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
  #include "esp_attr.h"
  #define FB_ATTR EXT_RAM_BSS_ATTR
#else
  #define FB_ATTR
#endif

// --- Off-screen framebuffer for the After Egypt scenes. Composed in
// memory then pushed as ONE SPI transaction via display_present_full —
// this eliminates the mid-frame clear/redraw flicker that the direct
// shrine_* pipeline produces on the ILI9341 (no vsync).
FB_ATTR static uint16_t s_fb[SCREEN_W * SCREEN_H];

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
