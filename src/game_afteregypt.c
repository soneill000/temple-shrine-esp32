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
// Auto-generated from AfterEgypt/Mountain.HC via tools/extract_sprite_tail.py.
// Provides SPRITE_MOUNTAIN_BI_1..7 opcode streams + a lookup table.
#include "sprite_mountain.h"
// Auto-generated from AfterEgypt/Battle.HC via tools/extract_sprite_tail.py.
// SPRITE_BATTLE_BI_1/2 = enemy warrior swing frames (vector).
// SPRITE_BATTLE_BI_3/4 = Moses arms up/down (vector).
#include "sprite_battle.h"
// Auto-generated from AfterEgypt/WaterRock.HC via tools/extract_sprite_tail.py.
// BI=1..4 = Moses rod-strike animation frames (vector).
// BI=5    = rock (SPT_BITMAP 30x19).
#include "sprite_waterrock.h"
// Auto-generated from AfterEgypt/GodTalking.HC via tools/extract_sprite_tail.py.
// BI=1/2 = burning bush blink frames (61x102 SPT_BITMAP each).
// BI=3   = Moses figure (46x60 SPT_BITMAP).
#include "sprite_godtalking.h"
// Auto-generated from AfterEgypt/Quail.HC via tools/extract_sprite_tail.py.
// BI=1/2 = alive quail wing-flap frames (vector).
// BI=3   = dead quail on ground (vector).
#include "sprite_quail.h"
// Auto-generated from AfterEgypt/Camp.HC via tools/extract_sprite_tail.py.
// BI=1..6 = 6 person walking frames (right + left, vector).
// BI=7    = tent sprite (SPT_BITMAP).
// BI=8    = golden calf (SPT_BITMAP).
#include "sprite_camp.h"
// Auto-generated from AfterEgypt/HorebA.HC via tools/extract_sprite_tail.py.
// BI=1 bush1 (also serves as burning bush), BI=2 bush2, BI=3 log,
// BI=4 tree1, BI=5 tree2 (bitmap), BI=6 sheep, BI=7 goat1, BI=8 goat2.
#include "sprite_horeba.h"
// Auto-generated from canewsin/templeos-1's aiwnios AESplash tail via
// tools/extract_sprite_tail_aiwnios.py. BI=1 is Terry's actual
// Moses-strikes-the-rock splash bitmap (640x589 @ (0,-117), SPT_BITMAP).
#include "sprite_aesplash.h"

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
    SC_COMICS,
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
    { "BEG FOR MEAT",  SC_QUAIL  },   // Terry's own name for this
    { "MT HOREB",      SC_HOREB  },
    // Terry's own Moses Comics needs Comics/*.DD.Z files from an ISO;
    // this entry ships homage panels drawn from our extracted sprites.
    { "STORY COMICS",  SC_COMICS },
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

// --- Verse wrap helpers, shared by multiple scenes ---
// Word-wrap a string into a table of line spans. Cheap enough to call
// once per verse at scene entry. Break-at-last-space; long words that
// exceed `cols` are split at `cols`. Returns lines populated.
#define VERSE_MAX_LINES 24

typedef struct {
    const char *start;
    uint8_t     len;
} verse_line_t;

static int wrap_lines(const char *s, int cols,
                      verse_line_t *out, int max_lines)
{
    int count = 0;
    while (*s && count < max_lines) {
        while (*s == ' ') s++;
        if (!*s) break;

        int n = 0, brk = 0;
        while (s[n] && n < cols) {
            if (s[n] == ' ') brk = n;
            n++;
        }
        if (s[n] && brk > 0) n = brk;

        out[count].start = s;
        out[count].len   = (uint8_t)(n > 255 ? 255 : n);
        count++;
        s += n;
    }
    return count;
}

// Draw a slice of wrapped lines [top .. top+visible) starting at
// (x, y_terry), advancing line_h_terry per row.
static void draw_wrapped_slice(CDC *dc, int x, int y_terry, int line_h_terry,
                               const verse_line_t *lines, int total,
                               int top, int visible)
{
    if (top < 0) top = 0;
    int end = top + visible;
    if (end > total) end = total;
    char buf[80];
    int cy = y_terry;
    for (int i = top; i < end; i++) {
        int len = lines[i].len;
        if (len > 79) len = 79;
        memcpy(buf, lines[i].start, len);
        buf[len] = 0;
        GrPrint(dc, x, cy, buf);
        cy += line_h_terry;
    }
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
        // Terry's HoldCourt PopUpPick verbatim: Show Mercy / Punish / Really Punish
        shrine_puts_centered(17, "  A  SHOW MERCY   ",  C_LTGREEN, C_BG);
        shrine_puts_centered(19, "  B  PUNISH       ",  C_YELLOW,  C_BG);
        shrine_puts_centered(21, " DN  REALLY PUNISH",  C_LTRED,   C_BG);
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
        case 1: result = "PUNISHED. THE CROWD NODS.";     rc = C_YELLOW;  break;
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
// Literal port of Terry's DrawGodTalking + UpTheMountain in GodTalking.HC.
//   Layout matches Terry's: 10 rows LTCYAN sky at top, YELLOW strip with
//   "God Says..." in red, then the bush/Moses tableau, then the wavy
//   ground plot. Mountain backdrop from Terry's source is skipped —
//   user reports it renders as a big cyan blob on the badge and Terry's
//   real scene reads as plain sky + yellow ground behind the bush/Moses
//   sprites and wavy ground line. Burning bush (BI=1/BI=2 blinking) at
//   (44, 99) and Moses (BI=3) at (213, 91) are Terry's real sprites.
//   Flame lines are Terry's exact loop: 256 random polar segments
//   around (235, 48) with radius 30, cycling color each frame.
//   Wavy ground is Terry's `if (tS%4.0<2.0) t=FullTri(...)` sine plot.
//   Words: A → random VOCAB word (approximates Terry's GodWord),
//          B → random Bible verse (approximates Terry's GodBiblePassage).

static void draw_wavy_ground(uint32_t now_ms)
{
    // Terry: if (tS%4.0<2.0) t=FullTri(tS, 2.0); else t=0;
    // FullTri returns a triangle wave in [-1..+1] over the period.
    float t = 0.0f;
    uint32_t phase = now_ms % 4000;
    if (phase < 2000) {
        float u = (float)phase / 2000.0f;             // 0..1
        t = (u < 0.5f) ? (u * 4.0f - 1.0f) : (3.0f - u * 4.0f);
    }
    gr_dc.color = C_BROWN;
    gr_dc.thick = 3;
    int lo = 10 + (int)(10.0f * (t < 0 ? -t : t));
    int hi = 130 - (int)(10.0f * (t < 0 ? -t : t));
    for (int i = lo; i < hi; i++) {
        int px = i + 10;
        int py = 110 + (int)(4.0f * t * sinf((float)i / 6.0f));
        GrPlot(&gr_dc, px, py);
    }
    gr_dc.thick = 1;
}

static void scene_god(void)
{
    // Terry's UpTheMountain sets Fs->text_attr = YELLOW<<4 + BLUE.
    // Sky region on top (10 rows * 8 = 80 Terry px), yellow strip below.
    const int sky_h    = 10 * 8;
    const int yellow_h = 3  * 8;

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    // Current "saying" state — cycled by A (word) or B (verse).
    const char *saying = NULL;
    const kjv_verse_t *saying_verse = NULL;

    uint32_t frame_i = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (shrine_key_pressed(BTN_A)) {
            saying = VOCAB[shrine_god(VOCAB_N)];
            saying_verse = NULL;
            shrine_beep(1600, 60);
            shrine_beep(2200, 80);
        }
        if (shrine_key_pressed(BTN_B)) {
            saying_verse = kjv_random();
            saying = NULL;
            shrine_beep(1000, 60);
            shrine_beep(1400, 60);
            shrine_beep(1800, 100);
        }

        uint32_t now = shrine_ms();
        frame_i++;

        // Region backgrounds.
        gr_dc.color = C_LTCYAN;
        GrFillRect(&gr_dc, 0, 0, 640, sky_h);
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, sky_h, 640, yellow_h);
        gr_dc.color = C_YELLOW;   // Terry doc default: YELLOW bg
        GrFillRect(&gr_dc, 0, sky_h + yellow_h, 640, 480 - sky_h - yellow_h);

        // "God Says..." header in the yellow strip (Terry: RED).
        gr_dc.color = C_LTRED;
        GrPrint(&gr_dc, 8, sky_h + 4, "GOD SAYS...");

        // Burning bush — Terry: if (Blink(0.4)) BI=1 else BI=2. Blink(0.4)
        // toggles every 0.4s. Position (44, 99) in Terry coords.
        if ((now / 400) & 1)
            Sprite3(&gr_dc, 44, 99, 0,
                    SPRITE_GODTALKING_BI_1, SPRITE_GODTALKING_BI_1_SIZE);
        else
            Sprite3(&gr_dc, 44, 99, 0,
                    SPRITE_GODTALKING_BI_2, SPRITE_GODTALKING_BI_2_SIZE);

        // Moses BI=3 at (213, 91).
        Sprite3(&gr_dc, 213, 91, 0,
                SPRITE_GODTALKING_BI_3, SPRITE_GODTALKING_BI_3_SIZE);

        // Flame lines — Terry's exact 256-line random polar shower
        // around center (235, 48), radius 30. Color cycles each frame.
        gr_dc.color = (color_t)(frame_i & 15);
        for (int i = 0; i < 128; i++) {   // halved from Terry's 256 for perf
            float m1 = (float)shrine_god(1000) / 1000.0f;
            float a1 = 6.28318f * (float)shrine_god(1000) / 1000.0f;
            float m2 = (float)shrine_god(1000) / 1000.0f;
            float a2 = 6.28318f * (float)shrine_god(1000) / 1000.0f;
            m1 *= m1; m2 *= m2;
            int x1 = 235 + (int)(30.0f * m1 * cosf(a1));
            int y1 =  56 + (int)(30.0f * m1 * sinf(a1));
            int x2 = 235 + (int)(30.0f * m2 * cosf(a2));
            int y2 =  40 + (int)(30.0f * m2 * sinf(a2));
            GrLine(&gr_dc, x1, y1, x2, y2);
        }

        // Wavy ground line.
        draw_wavy_ground(now);

        // Word / verse output — Terry cycles "God Says..." with 16
        // GodWord calls (or one GodBiblePassage). We show the current
        // pick in blue on the yellow doc.
        gr_dc.color = C_BLUE;
        if (saying_verse) {
            verse_line_t vlines[VERSE_MAX_LINES];
            int vcount = wrap_lines(saying_verse->text, 37,
                                    vlines, VERSE_MAX_LINES);
            draw_wrapped_slice(&gr_dc, 8, 260, 18, vlines, vcount, 0,
                               vcount < 9 ? vcount : 9);
            char cite[48];
            snprintf(cite, sizeof(cite), "-- %s %d:%d",
                     saying_verse->book,
                     saying_verse->chapter, saying_verse->verse);
            gr_dc.color = C_LTGREEN;
            GrPrint(&gr_dc, 8, 260 + 18 * 9 + 4, cite);
        } else if (saying) {
            // Center a bigger word in the middle of the doc.
            int nlen = 0; while (saying[nlen]) nlen++;
            int width_terry = nlen * 16;   // each glyph 8 fb = 16 Terry
            int sx = (640 - width_terry) / 2;
            GrPrint(&gr_dc, sx, 280, saying);
        } else {
            GrPrint(&gr_dc, 8, 260, "PRESS A FOR A WORD");
            GrPrint(&gr_dc, 8, 300, "PRESS B FOR A PASSAGE");
        }

        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 448, 464, "BOOT EXIT");

        DCPresent(&gr_dc);
        shrine_sleep_ms(60);
    }
}

// --- Water Rock scene ---
// Literal port of Terry's WaterRock.HC.
//   Yellow doc background. Exodus 17:6 verse at top. Moses (BI=1..4)
//   swings his rod at the rock (BI=5). Pressing SPACE (our BTN_A) DOWN
//   begins the down-stroke; releasing it starts the up-stroke. After
//   the rod has been down for DOWN_TIME=0.075s, a blue water circle
//   appears at the rock (radius grows at SPREAD_RATE=5 units/sec,
//   capped at 17). Terry uses SpriteInterpolate to blend frames; we
//   pick the nearest of the 4 poses instead.

#define WR_FRAMES      4
#define WR_DOWN_TIME_MS  75      // Terry: DOWN_TIME = 0.075 sec
#define WR_UP_TIME_MS   200      // Terry: UP_TIME   = 0.200 sec
#define WR_SPREAD_RATE   5       // Terry: SPREAD_RATE (units/sec)

static void scene_water(void)
{
    const int cx = 320, cy = 240;
    const uint8_t *const strike_imgs[WR_FRAMES] = {
        SPRITE_WATERROCK_BI_1, SPRITE_WATERROCK_BI_2,
        SPRITE_WATERROCK_BI_3, SPRITE_WATERROCK_BI_4,
    };
    const uint32_t strike_sizes[WR_FRAMES] = {
        SPRITE_WATERROCK_BI_1_SIZE, SPRITE_WATERROCK_BI_2_SIZE,
        SPRITE_WATERROCK_BI_3_SIZE, SPRITE_WATERROCK_BI_4_SIZE,
    };

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    verse_line_t vlines[VERSE_MAX_LINES];
    int vcount = 0;
    const kjv_verse_t *kv = kjv_lookup("EXODUS", 17, 6);
    if (kv) vcount = wrap_lines(kv->text, 37, vlines, VERSE_MAX_LINES);

    uint32_t t0_down     = 0;
    uint32_t t0_up       = 0;
    bool     down_stroke = false;
    bool     prev_held   = false;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_B)) return;

        uint32_t now  = shrine_ms();
        bool     held = shrine_key_held(BTN_A);

        // Edge detection for SPACE DOWN/UP.
        if (held && !prev_held) {
            t0_down     = now;
            down_stroke = true;
            shrine_beep(600, 40);
        } else if (!held && prev_held && t0_down) {
            t0_up       = now;
            down_stroke = false;
        }
        prev_held = held;

        // Frame BG.
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, 0, 640, 480);

        // Verse (blue on yellow). Exodus 17:6 wraps to 5 lines at 37 cols.
        gr_dc.color = C_BLUE;
        draw_wrapped_slice(&gr_dc, 8, 8, 18, vlines, vcount, 0,
                           vcount < 5 ? vcount : 5);

        // Water spread — Terry: if t0_down set and elapsed > DOWN_TIME,
        // draw blue circle at (cx-63, cy-20) with radius = SPREAD_RATE *
        // (elapsed - DOWN_TIME), capped at 17. On top of that, we spray
        // fast-flying droplets while the down-stroke is active so the
        // hit lands with kinetic punch (badge addendum on top of Terry).
        if (t0_down) {
            uint32_t elapsed = now - t0_down;
            if (elapsed > WR_DOWN_TIME_MS) {
                int r = (WR_SPREAD_RATE * (int)(elapsed - WR_DOWN_TIME_MS)) / 1000;
                if (r > 17) r = 17;
                if (r >= 2) {
                    gr_dc.color = C_BLUE;
                    GrFillCircle(&gr_dc, cx - 63, cy - 20, r);
                }
            }
        }
        // Droplet spray on impact — 24 short streaks in cyan/blue at
        // the rock face, angled up-and-outward.
        if (down_stroke) {
            for (int p = 0; p < 24; p++) {
                float a = -3.14159f * 0.25f - 3.14159f * 0.5f *
                          ((float)shrine_god(1000) / 1000.0f);
                float m = 30.0f + (float)shrine_god(50);
                int px = cx - 63 + (int)(m * cosf(a));
                int py = cy - 20 + (int)(m * sinf(a));
                gr_dc.color = (p & 1) ? C_LTBLUE : C_LTCYAN;
                GrPlot(&gr_dc, px,     py);
                GrPlot(&gr_dc, px + 1, py);
                GrPlot(&gr_dc, px,     py + 1);
            }
        }

        // Compute tt (Moses animation phase, 0..1).
        float tt;
        if (down_stroke) {
            tt = (float)(now - t0_down) / (float)WR_DOWN_TIME_MS;
            if (tt > 0.9999f) tt = 0.9999f;
        } else if (t0_up) {
            tt = (float)(now - t0_up) / (float)WR_UP_TIME_MS;
            if (tt > 0.9999f) tt = 0.9999f;
            tt = 1.0f - tt;
        } else {
            tt = 1.0f;   // resting pose (arm up) before any stroke
        }
        int i = (int)((WR_FRAMES - 1) * tt);
        if (i < 0) i = 0;
        if (i > WR_FRAMES - 1) i = WR_FRAMES - 1;

        // Draw Moses at (cx, cy).
        Sprite3(&gr_dc, cx, cy, 0, strike_imgs[i], strike_sizes[i]);

        // Draw Rock at (cx-64, cy-4). BI=5 is the 30x19 bitmap.
        Sprite3(&gr_dc, cx - 64, cy - 4, 0,
                SPRITE_WATERROCK_BI_5, SPRITE_WATERROCK_BI_5_SIZE);

        // Hint text (Terry: "$$BK,1$$<SPACE>$$BK,0$$" — blinking).
        // Positioned below the 4-line verse strip (ends ~Terry y=90).
        gr_dc.color = ((now / 500) & 1) ? C_LTRED : C_BLUE;
        GrPrint(&gr_dc, 8, 110, "HOLD A - STRIKE");

        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 448, 464, "B EXIT");

        DCPresent(&gr_dc);
        shrine_sleep_ms(20);
    }
}

// --- Battle scene ---
// Literal port of Terry's Battle.HC:
//   Moses at (cx, cy+SPACING) shows BI=3 (arms up) or BI=4 (arms down),
//   with a linear interp between them driven by tt = ((tS-t0)/2)^4.
//   Three Amalekite warriors at (xx+cx+45, cy-45), (xx+cx+90, cy-45),
//   (xx+cx+45, cy-90) sawtooth between BI=1 and BI=2 each HACK_PERIOD.
//   Holding SPACE (our BTN_A) forces arms-up (tt=0); releasing lets tt
//   climb, and the enemy line drifts (xx accumulates) — Amalek prevails
//   whenever Moses's hand drops.
//
// SpriteInterpolate blends two op streams; we don't have that yet, so
// we pick the near-endpoint stream by rounding tt to 0 or 1. Two-frame
// animation of vector sprites reads correctly enough on a small screen.
//
// Exit on BOOT (or the menu-standard BTN_A tap — but here A is our
// SPACE, so we use BTN_B for exit to keep the mechanic intact).

#define HACK_PERIOD_MS  250     // Terry: HACK_PERIOD = 0.25 sec
#define BATTLE_SPACING  45      // Terry: SPACING

static inline float saw_ms(uint32_t now_ms, uint32_t phase_ms, uint32_t period_ms)
{
    // Terry's Saw(tS + phase, period) — sawtooth in [0..1] over `period`.
    uint32_t t = (now_ms + phase_ms) % period_ms;
    return (float)t / (float)period_ms;
}

static void scene_battle(void)
{
    const int cx = 320, cy = 240;

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    // Verse strip (Terry: BibleVerse Exodus 17:11, size 8 rows).
    verse_line_t vlines[VERSE_MAX_LINES];
    int vcount = 0;
    const kjv_verse_t *kv = kjv_lookup("EXODUS", 17, 11);
    if (kv) vcount = wrap_lines(kv->text, 37, vlines, VERSE_MAX_LINES);

    uint32_t t0     = shrine_ms();      // Terry's `t0 = tS` reset when SPACE releases
    float    xx     = 0.0f;             // enemy-line horizontal drift
    uint32_t t_last = 0;                // ms of previous frame, for dt

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_B)) return;

        uint32_t now  = shrine_ms();
        bool     held = shrine_key_held(BTN_A);   // Terry: held_up = SPACE held
        if (!held && (now - t_last < 40)) {
            // On key-release, Terry resets t0 = tS. We approximate: whenever
            // the key transitions from held to released we reset t0.
        }
        // Simple state tracking for the t0 reset on release.
        static bool prev_held = false;
        if (prev_held && !held) t0 = now;
        prev_held = held;

        // Terry: if (held_up) tt = 0 else tt = ((tS-t0)/2)^4, capped at 1.
        float tt;
        if (held) tt = 0.0f;
        else {
            float s = ((float)(now - t0) / 1000.0f) / 2.0f;
            s = s * s; s = s * s;
            tt = s > 1.0f ? 1.0f : s;
        }

        // Terry: xx drifts left while tt<0.5, right while tt>=0.5, at
        // 50 px/sec. Positive xx means Amalek is prevailing (advancing).
        if (t_last) {
            float dt = (float)(now - t_last) / 1000.0f;
            xx += (tt < 0.5f ? -50.0f : 50.0f) * dt;
        }
        t_last = now;

        // Clamp xx so the enemy line doesn't wander off screen forever.
        if (xx < -200.0f) xx = -200.0f;
        if (xx >  200.0f) xx =  200.0f;

        // Frame — repaint doc-background yellow (Terry's $$BG,YELLOW$$).
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, 0, 640, 480);

        // Verse at top in blue on yellow (Terry: text_attr YELLOW/BLUE).
        // Exodus 17:11 wraps to 4 lines at 37 cols.
        gr_dc.color = C_BLUE;
        draw_wrapped_slice(&gr_dc, 8, 8, 18, vlines, vcount, 0,
                           vcount < 4 ? vcount : 4);

        // Hint text (Terry: "Hold <SPACE>"). Below 4-line verse strip
        // which ends around Terry y=88.
        gr_dc.color = held ? C_LTGREEN : C_LTRED;
        GrPrint(&gr_dc, 8, 104, "HOLD A - MOSES'S ARMS");

        // Moses at (cx, cy+SPACING). BI=3 arms up, BI=4 arms down. Pick
        // the near-endpoint sprite as SpriteInterpolate substitute.
        if (tt < 0.5f)
            Sprite3(&gr_dc, cx, cy + BATTLE_SPACING, 0,
                    SPRITE_BATTLE_BI_3, SPRITE_BATTLE_BI_3_SIZE);
        else
            Sprite3(&gr_dc, cx, cy + BATTLE_SPACING, 0,
                    SPRITE_BATTLE_BI_4, SPRITE_BATTLE_BI_4_SIZE);

        // Three Amalekite warriors — each phases through BI=1/BI=2
        // sawtooth animation at a HACK_PERIOD offset (Terry: 0.0, 0.333,
        // 0.666 of HACK_PERIOD).
        const struct { int ox, oy; uint32_t phase_ms; } warriors[3] = {
            { BATTLE_SPACING,      -BATTLE_SPACING,     0                       },
            { BATTLE_SPACING * 2,  -BATTLE_SPACING,     HACK_PERIOD_MS / 3      },
            { BATTLE_SPACING,      -BATTLE_SPACING * 2, (HACK_PERIOD_MS * 2)/3 },
        };
        for (int w = 0; w < 3; w++) {
            float phase = saw_ms(now, warriors[w].phase_ms, HACK_PERIOD_MS);
            phase *= 2.0f; if (phase > 1.0f) phase = 2.0f - phase;
            int wx = (int)xx + cx + warriors[w].ox;
            int wy = cy + warriors[w].oy;
            if (phase < 0.5f)
                Sprite3(&gr_dc, wx, wy, 0,
                        SPRITE_BATTLE_BI_1, SPRITE_BATTLE_BI_1_SIZE);
            else
                Sprite3(&gr_dc, wx, wy, 0,
                        SPRITE_BATTLE_BI_2, SPRITE_BATTLE_BI_2_SIZE);
        }

        // Bottom-line status (our own — Terry has none).
        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 8,   464, xx > 0 ? "AMALEK PREVAILS" : "ISRAEL PREVAILS");
        GrPrint(&gr_dc, 448, 464, "B EXIT");

        DCPresent(&gr_dc);
        shrine_sleep_ms(30);
    }
}

// --- Quail scene ---
// Literal port of Terry's Quail.HC.
//   SKY_HEIGHT = 0.6 * GR_HEIGHT = 288 (Terry). QUAIL_NUM = 128 quail
//   flock the sky region; each has (x, y, dx, dy, phase, dead). Alive
//   quail flap wings via BI=1/BI=2 (Terry interpolates; we pick
//   nearest phase). Occasionally a quail dies mid-flight and falls
//   with gravity to the ground, then holds BI=3.
//
//   Terry's death trigger `if (x>0 && t1-t_last>10*Rand)` reads as
//   "small per-frame chance per quail" — we mirror with ~0.4%/frame.
//   Verse: Terry cites Numbers 11:11 (Moses complaining about the
//   burden — the whole "quail from heaven" story is God's reply).

#define QUAIL_NUM        64                    // Terry: 128; halved for perf
#define QUAIL_SKY_TERRY  288                   // Terry: SKY_HEIGHT
#define QUAIL_VERSE_H    88                    // top strip for verse

typedef struct {
    float x, y;
    float dx, dy;
    float phase;
    bool  dead;
} quail_t;
static quail_t s_quail[QUAIL_NUM];

static void scene_quail(void)
{
    // Init the flock — Terry's Quail() setup.
    for (int i = 0; i < QUAIL_NUM; i++) {
        s_quail[i].x     = (float)((int)shrine_god(640));         // 0..GR_WIDTH
        s_quail[i].y     = (float)shrine_god(QUAIL_SKY_TERRY - QUAIL_VERSE_H)
                           + QUAIL_VERSE_H;
        s_quail[i].dx    = (float)shrine_god(50) + 10.0f;          // 10..60 Terry/sec
        s_quail[i].dy    = ((float)shrine_god(20) - 10.0f);        // -10..+10
        s_quail[i].phase = (float)shrine_god(1000) / 1000.0f;
        s_quail[i].dead  = false;
    }

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    // Pre-wrap Numbers 11:11.
    verse_line_t vlines[VERSE_MAX_LINES];
    int vcount = 0;
    const kjv_verse_t *kv = kjv_lookup("NUMBERS", 11, 11);
    if (kv) vcount = wrap_lines(kv->text, 37, vlines, VERSE_MAX_LINES);

    uint32_t t0     = shrine_ms();
    uint32_t t_last = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        // Terry's Quail() ends with a plain `PressAKey;` — watch and
        // exit on any button. No invented mechanics.
        if (shrine_key_pressed(BTN_A) || shrine_key_pressed(BTN_B)) return;

        uint32_t now = shrine_ms();
        float t1 = (float)(now - t0) / 1000.0f;
        // dt is elapsed since previous frame, both in ms then convert.
        float dt = t_last ? ((float)(now - t_last) / 1000.0f) : 0.0f;
        if (dt > 0.2f) dt = 0.2f;   // don't skip too far on hiccup

        // Frame BG — LTCYAN sky (Terry: $$BG,LTCYAN$$).
        gr_dc.color = C_LTCYAN;
        GrFillRect(&gr_dc, 0, 0, 640, QUAIL_SKY_TERRY);
        // Yellow strip below sky (Terry: 5-row YELLOW strip before doc).
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, QUAIL_SKY_TERRY, 640, 480 - QUAIL_SKY_TERRY);

        // Terry's DrawQuail draws the Mountain BI=4 pointer at
        // (0, SKY_HEIGHT), but on-badge that sprite reads as a big cyan
        // slab that hides the quail flock. User reports Terry's actual
        // scene is just sky + yellow ground behind the quail. Skipping
        // the mountain backdrop here.

        // Verse in blue on the top of the sky.
        gr_dc.color = C_BLUE;
        draw_wrapped_slice(&gr_dc, 8, 8, 18, vlines, vcount, 0,
                           vcount < 4 ? vcount : 4);

        // Update + draw each quail.
        for (int i = 0; i < QUAIL_NUM; i++) {
            quail_t *q = &s_quail[i];

            if (q->dead) {
                // Terry: falls with gravity, dx stops at ground.
                q->x += dt * q->dx;
                q->y += dt * 50.0f;
                if (q->y > QUAIL_SKY_TERRY) {
                    q->y  = QUAIL_SKY_TERRY;
                    q->dx = 0.0f;
                }
                Sprite3(&gr_dc, (int)q->x, (int)q->y, 0,
                        SPRITE_QUAIL_BI_3, SPRITE_QUAIL_BI_3_SIZE);
            } else {
                // Terry: fly at (dx, dy); bounce vertically at sky edges.
                q->x += dt * q->dx;
                q->y += dt * q->dy;
                if (q->y < QUAIL_VERSE_H || q->y > QUAIL_SKY_TERRY - 20) {
                    q->dy = -q->dy;
                    q->y += dt * q->dy;
                }
                // Wrap x horizontally so the flock doesn't drift off.
                if (q->x > 660) q->x = -20;

                // Wing-flap frame — Terry: SpriteInterpolate(tt, BI=1, BI=2)
                // with tt = Tri(t1 + phase, 1.0) (triangle wave 0..1).
                float u = t1 + q->phase;
                u -= (int)u;
                float tt = u < 0.5f ? u * 2.0f : 2.0f - u * 2.0f;
                if (tt < 0.5f)
                    Sprite3(&gr_dc, (int)q->x, (int)q->y, 0,
                            SPRITE_QUAIL_BI_1, SPRITE_QUAIL_BI_1_SIZE);
                else
                    Sprite3(&gr_dc, (int)q->x, (int)q->y, 0,
                            SPRITE_QUAIL_BI_2, SPRITE_QUAIL_BI_2_SIZE);

                // Terry: `if (q[i].x>0 && t1-t_last>10*Rand)` — a rare
                // random death per frame. We approximate with a small
                // constant probability that reads similarly on our tick.
                if (q->x > 0.0f && shrine_god(1000) < 3) {
                    q->dead = true;
                    shrine_beep(600, 20);
                }
            }
        }

        // Terry's Quail has no HUD — just the scene and a PressAKey exit.
        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 448, 464, "A/BOOT EXIT");

        DCPresent(&gr_dc);
        t_last = now;
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

static void scene_clouds(void)
{
    // Terry's Clouds.HC uses SKY_LINES=30 rows of 8-pixel text as the
    // sky region, then 5 rows of yellow strip, then the Bible verse
    // occupies the rest of the doc. Layout in Terry-space (640x480):
    //     y=0..240   LTCYAN sky
    //     y=240..280 YELLOW strip (5*8 rows)
    //     y=280..480 verse text on the default BLUE document background
    const int sky_h        = 30 * 8;   // Terry: SKY_LINES * FONT_HEIGHT
    const int yellow_h     = 5  * 8;
    const int verse_x      = 8;
    const int verse_y      = sky_h + yellow_h + 8;   // 288 Terry
    const int verse_lh     = 18;                     // 9 fb px per line
    const int verse_cols   = 37;                     // fits in 320 fb width
    const int verse_vis    = 9;                      // rows on screen

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);
    clouds_init();

    // Pre-wrap the verse once. Terry's BibleVerse pipes through DolDoc,
    // which handles wrapping natively; we do it up front.
    verse_line_t vlines[VERSE_MAX_LINES];
    int vcount = 0;
    const kjv_verse_t *kv = kjv_lookup("EXODUS", 14, 19);
    if (kv) vcount = wrap_lines(kv->text, verse_cols, vlines, VERSE_MAX_LINES);
    int vtop_max = vcount > verse_vis ? vcount - verse_vis : 0;
    int vtop = 0;

    uint32_t last = shrine_ms();
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;

        // Scroll the verse — UP/DOWN pages one line at a time.
        if (shrine_key_pressed(BTN_UP)   && vtop > 0)        vtop--;
        if (shrine_key_pressed(BTN_DOWN) && vtop < vtop_max) vtop++;

        uint32_t now = shrine_ms();
        if (now - last > 20) { clouds_animate(); last = now; }

        // Region backgrounds — mirror Terry's overrides. Doc default
        // is Fs->text_attr = YELLOW<<4 + BLUE (bg YELLOW, fg BLUE); the
        // sky and yellow strip use $$BG,LTCYAN$$ and $$BG,YELLOW$$
        // overrides. Verse region uses the doc default: YELLOW bg.
        gr_dc.color = C_LTCYAN;
        GrFillRect(&gr_dc, 0, 0,     640, sky_h);
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, sky_h, 640, 480 - sky_h);   // yellow strip + verse

        // Mountain intentionally omitted from Clouds per user preference
        // (Terry's Clouds.HC does Sprite3 the Mountain here — noted for
        // possible revisit later).

        // Clouds — Terry's DrawIt calls MPDrawClouds workers.
        clouds_drawit();

        // Bible verse — Terry calls BibleVerse(,"Exodus,14:19",7) which
        // renders under the doc's default YELLOW/BLUE attr.
        if (vcount) {
            gr_dc.color = C_BLUE;
            draw_wrapped_slice(&gr_dc, verse_x, verse_y, verse_lh,
                               vlines, vcount, vtop, verse_vis);
            // Scroll chevrons in the right margin, same blue as text.
            if (vtop > 0)
                GrPrint(&gr_dc, 608, verse_y, "^");
            if (vtop < vtop_max)
                GrPrint(&gr_dc, 608, verse_y + (verse_vis - 1) * verse_lh, "v");
        }
        gr_dc.color = C_BLUE;
        GrPrint(&gr_dc, 8, 452, "-- EXODUS 14:19");

        // Chrome — title top-left of sky, controls top-right.
        gr_dc.color = C_BLACK;
        GrPrint(&gr_dc, 8, sky_h + 8, "VIEW CLOUDS");
        gr_dc.color = C_BLACK;
        GrPrint(&gr_dc, 448, sky_h + 8,
                vtop_max ? "UP/DN SCROLL" : "A/BOOT EXIT");

        DCPresent(&gr_dc);
        shrine_sleep_ms(40);
    }
}

// --- View Map scene ---
// Literal port of Terry's AEMap in Map.HC. His "40 years of wandering"
// gag: two random-walk angles (fine a1 in ±0.05, coarse a2 in ±0.30)
// drive a pilgrim's position across the yellow map; a black line trail
// records the path.  Terry also renders a walker sprite via DrawMap()
// selecting from left_imgs[]/right_imgs[]; those bitmaps live in the
// Mountain.HC.Z blob we haven't decoded yet, so the walker is deferred
// (line trail alone still tells the "wandering forever" story).
//
// Terry runs this on a 640x480 doc; we render in Terry-space with the
// shim scaling to our 320x240 fb.

#define AE_A1_MIN 0.02f
#define AE_A1_MAX 0.05f
#define AE_A2_MIN 0.15f
#define AE_A2_MAX 0.30f

static inline float rand01(void)
{
    return (float)shrine_god(1024) / 1024.0f;
}

#define MAP_TRAIL_MAX 512

static void scene_map(void)
{
    // Layout matches Terry's: yellow doc background, verse strip at top,
    // wander region is the rest.  Wander starts below the 5-line verse
    // strip; we redraw it fresh each frame from a ring buffer of trail
    // segments so the walker sprite can be drawn cleanly on top.
    const int cx           = 320;
    const int cy           = 290;
    const int cmin         = 190;
    const int wander_top   = 100;   // Terry-space top of wander region

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    // Fill entire doc with YELLOW once (Terry: "$$BG,YELLOW$$%h*c").
    gr_dc.color = C_YELLOW;
    GrFillRect(&gr_dc, 0, 0, 640, 480);

    // Render BibleVerse("Exodus,16:35", 3) into a fixed strip at top.
    // Verse doesn't change during the scene — draw once, then never
    // touch that region again while the wander loop redraws below.
    verse_line_t vlines[VERSE_MAX_LINES];
    int vcount = 0;
    const kjv_verse_t *kv = kjv_lookup("EXODUS", 16, 35);
    if (kv) {
        vcount = wrap_lines(kv->text, 37, vlines, VERSE_MAX_LINES);
        gr_dc.color = C_BLACK;
        draw_wrapped_slice(&gr_dc, 8, 8, 18, vlines, vcount, 0,
                           vcount < 5 ? vcount : 5);
    }

    // Corner chrome so the exit path is discoverable.
    gr_dc.color = C_DKGRAY;
    GrPrint(&gr_dc, 448, 464, "A/BOOT EXIT");

    // Trail ring buffer — each entry is (x1, y1, x2, y2) in Terry coords.
    // 512 segments (~8KB) is plenty for a long wander; once full we
    // start overwriting oldest so the tail of the path fades naturally.
    static int16_t trail[MAP_TRAIL_MAX][4];
    int trail_n     = 0;   // number of segments currently stored
    int trail_head  = 0;   // ring index of oldest segment

    // Walker sprite frame tables — Terry's right_imgs/left_imgs in
    // Mountain.HC. 4-frame walk cycles indexed by (6.0*tS)&3, i.e. the
    // animation phase rolls at 6Hz.
    static const uint8_t *const RIGHT_FRAMES[4] = {
        SPRITE_MOUNTAIN_BI_2, SPRITE_MOUNTAIN_BI_3,
        SPRITE_MOUNTAIN_BI_4, SPRITE_MOUNTAIN_BI_3,
    };
    static const uint32_t RIGHT_SIZES[4] = {
        SPRITE_MOUNTAIN_BI_2_SIZE, SPRITE_MOUNTAIN_BI_3_SIZE,
        SPRITE_MOUNTAIN_BI_4_SIZE, SPRITE_MOUNTAIN_BI_3_SIZE,
    };
    static const uint8_t *const LEFT_FRAMES[4] = {
        SPRITE_MOUNTAIN_BI_5, SPRITE_MOUNTAIN_BI_6,
        SPRITE_MOUNTAIN_BI_5, SPRITE_MOUNTAIN_BI_7,
    };
    static const uint32_t LEFT_SIZES[4] = {
        SPRITE_MOUNTAIN_BI_5_SIZE, SPRITE_MOUNTAIN_BI_6_SIZE,
        SPRITE_MOUNTAIN_BI_5_SIZE, SPRITE_MOUNTAIN_BI_7_SIZE,
    };

    // Wander state — mirrors Terry's local declarations.
    float a1        = (AE_A1_MAX + AE_A1_MIN) / 2.0f;
    float a2        = (AE_A2_MAX + AE_A2_MIN) / 2.0f;
    float a2_total  = a2;
    float x1 = 0.0f, y1 = 0.0f;
    int   x = cx, y = cy;
    int   x_last = x, y_last = y;
    int   i = 0;
    bool  last_left = false;   // Terry's DrawMap `static Bool last_left`

    DCPresent(&gr_dc);

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;

        // --- Terry's a1 update: random step, clamp to ±MAX, snap out
        //     of the ±MIN dead-zone so the walker keeps turning.
        float step1 = (AE_A1_MAX + AE_A1_MIN) / 5.0f;
        a1 += step1 * (rand01() - 0.5f);
        if (a1 >  AE_A1_MAX) a1 =  AE_A1_MAX;
        if (a1 < -AE_A1_MAX) a1 = -AE_A1_MAX;
        float a = a1;
        if (a >= 0.0f && a <=  AE_A1_MIN) a =  AE_A1_MIN;
        if (a <= 0.0f && a >= -AE_A1_MIN) a = -AE_A1_MIN;

        // Rotate (x1, y1) by `a` around Z (matches Mat4x4RotZ +
        // Mat4x4MulXYZ in Terry's HolyC).
        float cs = cosf(a), sn = sinf(a);
        float nx1 = cs * x1 - sn * y1;
        float ny1 = sn * x1 + cs * y1;
        x1 = nx1; y1 = ny1;

        // --- Terry's a2 update: same random-walk + dead-zone snap.
        float step2 = (AE_A2_MAX + AE_A2_MIN) / 5.0f;
        a2 += step2 * (rand01() - 0.5f);
        if (a2 >  AE_A2_MAX) a2 =  AE_A2_MAX;
        if (a2 < -AE_A2_MAX) a2 = -AE_A2_MAX;
        float a2i = a2;
        if (a2i >= 0.0f && a2i <=  AE_A2_MIN) a2i =  AE_A2_MIN;
        if (a2i <= 0.0f && a2i >= -AE_A2_MIN) a2i = -AE_A2_MIN;
        a2_total += a2i;

        // Advance position 6 units along the current heading, clamped
        // to the wander square.
        x1 += 6.0f * cosf(a2_total);
        y1 += 6.0f * sinf(a2_total);
        if (x1 < -(cmin - 10)) x1 = -(cmin - 10);
        if (x1 >  (cmin - 10)) x1 =  (cmin - 10);
        if (y1 < -(cmin - 10)) y1 = -(cmin - 10);
        if (y1 >  (cmin - 10)) y1 =  (cmin - 10);

        // Terry scales (x1, y1) anisotropically to fill the screen.
        // Our cy leaves room for the verse strip, so scale y a touch
        // tighter than x to stay clear of the top text.
        x = (int)(1.6f * x1) + cx;
        y = (int)(0.9f * y1) + cy;

        // Every other iteration append a segment — Terry's `if (i++&1)`.
        if (i++ & 1) {
            int slot = (trail_head + trail_n) % MAP_TRAIL_MAX;
            trail[slot][0] = (int16_t)x_last;
            trail[slot][1] = (int16_t)y_last;
            trail[slot][2] = (int16_t)x;
            trail[slot][3] = (int16_t)y;
            if (trail_n < MAP_TRAIL_MAX) trail_n++;
            else trail_head = (trail_head + 1) % MAP_TRAIL_MAX;
        }

        // Repaint wander area yellow, then replay the entire trail so
        // we can draw a fresh walker sprite without smearing.
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, wander_top, 640, 480 - wander_top);
        gr_dc.color = C_BLACK;
        for (int j = 0; j < trail_n; j++) {
            int k = (trail_head + j) % MAP_TRAIL_MAX;
            GrLine(&gr_dc,
                   trail[k][0], trail[k][1],
                   trail[k][2], trail[k][3]);
        }

        // Walker sprite — Terry's DrawMap: pick left/right imgs based
        // on last direction, frame based on animation phase.
        if      (x < x_last) last_left = true;
        else if (x > x_last) last_left = false;
        int frame = (int)((shrine_ms() / 167) & 3);   // Terry: 6Hz -> (6.0*tS)&3
        if (last_left)
            Sprite3(&gr_dc, x, y, 0,
                    LEFT_FRAMES[frame],  LEFT_SIZES[frame]);
        else
            Sprite3(&gr_dc, x, y, 0,
                    RIGHT_FRAMES[frame], RIGHT_SIZES[frame]);

        // Bottom-right chrome sits inside the wander region so we redraw
        // it after the yellow fill.
        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 448, 464, "A/BOOT EXIT");

        x_last = x; y_last = y;

        DCPresent(&gr_dc);
        shrine_sleep_ms(15);   // Terry: Sleep(15)
    }
}

// --- Break Camp scene ---
// Literal port of Terry's Camp.HC.
//   The Israelite camp seen through a slowly-orbiting view point:
//   people wander in a random cloud, then every ~5 sec re-form into a
//   circle (Terry's `if (tS%10<5) random; else form-circle`). During
//   circle-formation there's a 50% chance the Golden Calf (BI=8)
//   appears in the middle with a blinking "!!Golden Calf!!" caption.
//   Tents (BI=7) sit around the camp perimeter. People animate through
//   4 frames of walking (right BI=1/2/3 or left BI=4/5/6, matching
//   Terry's imgs arrays), picking direction from their dx sign.
//
//   Terry's version runs 100 people; we run 60 for perf on the badge.
//   Terry's Sprite3B uses depth-buffered rendering; we sort objects
//   back-to-front and use Sprite3S with per-object depth-scale.
//   Mountain backdrop (BI=9) is a Mountain.HC.Z ref — omitted for now.

#define CAMP_PEOPLE      100    // Terry runs 1024; we're ~10% for perf headroom
#define CAMP_TENT_RATIO  20
#define CAMP_TENTS       ((CAMP_PEOPLE + CAMP_TENT_RATIO - 1) / CAMP_TENT_RATIO)
#define CAMP_OBJS        (CAMP_PEOPLE + CAMP_TENTS)
#define CAMP_WIDTH       600
#define CAMP_SCALE_F     300.0f
#define CAMP_Z_CLIP      10.0f

typedef enum { COT_PERSON = 1, COT_TENT = 2 } camp_obj_type_t;

typedef struct {
    float           x, y, z;     // world coords
    float           dx, dy, dz;  // velocity (people only)
    camp_obj_type_t type;
} camp_obj_t;

static camp_obj_t s_camp_objs[CAMP_OBJS];

// Per-frame draw record (used for painter's-algorithm sort).
typedef struct {
    int   screen_x, screen_y;
    float depth_z;      // eye-space depth (positive = further)
    float scale;
    int   obj_index;    // index back into s_camp_objs
} camp_draw_t;
// +1 slot so we can fold the Golden Calf into the sorted draw list
// (obj_index=-1 marks it) — fixes calf-always-on-top overlap bug.
static camp_draw_t s_camp_draws[CAMP_OBJS + 1];

// Terry's CTransform: subtract view point, rotate 23deg X, perspective
// divide when the object is in front of the camera. Returns true if the
// object is visible; screen coords + z stored in *out.
static bool camp_transform(float wx, float wy, float wz,
                           float vpx, float vpy, float vpz,
                           float rot_x_cos, float rot_x_sin,
                           int   cx, int cy,
                           camp_draw_t *out)
{
    // Subtract view point.
    float x = wx - vpx;
    float y = wy - vpy;
    float z = wz - vpz;

    // Rotate about X (Terry: Mat4x4RotX(r1, 23*pi/180)).
    float ny = y * rot_x_cos - z * rot_x_sin;
    float nz = y * rot_x_sin + z * rot_x_cos;
    y = ny; z = nz;

    // Terry's CTransform projects only when z < 0 (in front of camera).
    if (z >= 0.0f) return false;

    float projx = x * -CAMP_SCALE_F / z;
    float projy = y * -CAMP_SCALE_F / z;

    // Terry: *_z = dc->z - z  (positive = further away).
    float dz = -z;
    if (dz <= CAMP_Z_CLIP) return false;

    out->screen_x = cx + (int)projx;
    out->screen_y = cy - (int)projy;
    out->depth_z  = dz;
    // Terry: s = 0.5 * CAMP_SCALE / z. On his 640x480 screen the
    // sprite already fills a chunk; on our 320x240 those pixels
    // shrink to near-invisible dots, so we bump the base by 3x.
    out->scale    = 1.5f * CAMP_SCALE_F / dz;
    return true;
}

static int camp_draw_cmp(const void *a, const void *b)
{
    // Sort back-to-front (larger depth first).
    float za = ((const camp_draw_t *)a)->depth_z;
    float zb = ((const camp_draw_t *)b)->depth_z;
    if (za < zb) return  1;
    if (za > zb) return -1;
    return 0;
}

static void scene_camp(void)
{
    // Initialize people + tents at random world positions inside the
    // camp square (Terry: -CAMP_WIDTH/2..CAMP_WIDTH/2 in x, -CAMP_WIDTH..0 in z).
    for (int i = 0; i < CAMP_PEOPLE; i++) {
        s_camp_objs[i].x = (float)((int)shrine_god(CAMP_WIDTH) - CAMP_WIDTH / 2);
        s_camp_objs[i].y = 0.0f;
        s_camp_objs[i].z = -(float)shrine_god(CAMP_WIDTH);
        s_camp_objs[i].dx = 0.0f;
        s_camp_objs[i].dy = 0.0f;
        s_camp_objs[i].dz = 0.0f;
        s_camp_objs[i].type = COT_PERSON;
    }
    for (int i = 0; i < CAMP_TENTS; i++) {
        int j = CAMP_PEOPLE + i;
        s_camp_objs[j].x = (float)((int)shrine_god(CAMP_WIDTH) - CAMP_WIDTH / 2);
        s_camp_objs[j].y = 0.0f;
        s_camp_objs[j].z = -(float)shrine_god(CAMP_WIDTH);
        s_camp_objs[j].type = COT_TENT;
    }

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    const float rot_x     = 23.0f * 3.14159265f / 180.0f;
    const float rot_cos_x = cosf(rot_x);
    const float rot_sin_x = sinf(rot_x);

    // Golden calf event state.
    bool     calf        = false;
    bool     determined  = false;
    uint32_t last_cycle  = 0;
    uint32_t t0          = shrine_ms();
    uint32_t t_last      = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;

        uint32_t now = shrine_ms();
        float    tS  = (float)(now - t0) / 1000.0f;
        float    dt  = t_last ? (float)(now - t_last) / 1000.0f : 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        t_last = now;

        // Terry: camp_view_pt animates on a slow figure-8.
        float vpx = 0.0f;
        float vpy = 200.0f - 100.0f * sinf(tS);
        float vpz = 225.0f + 100.0f * cosf(tS);

        // Terry: 10-sec cycle. First 5s = wander, second 5s = form circle.
        uint32_t cycle_ms = (uint32_t)(now % 10000);
        bool     forming  = (cycle_ms >= 5000);
        if (!forming) {
            calf       = false;
            determined = false;
        } else if (!determined) {
            // 50% chance of calf on this circle.
            determined = true;
            calf       = ((int)shrine_god(1000) < 500);
        }
        last_cycle = cycle_ms;

        // Update people.
        const float SPEED_MAX = 30.0f;   // Terry: SPEED_MAX = 2<<32, in fixed-point
        for (int i = 0; i < CAMP_PEOPLE; i++) {
            camp_obj_t *o = &s_camp_objs[i];
            if (!forming) {
                // Random walk.
                o->dx += ((float)shrine_god(1000) / 500.0f - 1.0f) * 20.0f * dt;
                o->dz += ((float)shrine_god(1000) / 500.0f - 1.0f) * 20.0f * dt;
            } else {
                // Steer toward a target on a circle around the camp
                // center — Terry's Cos/Sin(i * 2*pi/n) placement.
                float ang = (1.0f + 1.0f / CAMP_TENT_RATIO)
                            * (float)i * 6.28318f / (float)CAMP_PEOPLE;
                float tx  = (float)(CAMP_WIDTH / 4) * cosf(ang);
                float tz  = (float)(CAMP_WIDTH / 4) * sinf(ang) - CAMP_WIDTH / 2.0f;
                float ddx = tx - o->x;
                float ddz = tz - o->z;
                float d   = sqrtf(ddx * ddx + ddz * ddz);
                if (d > 0.001f) {
                    o->dx = SPEED_MAX * ddx / d;
                    o->dz = SPEED_MAX * ddz / d;
                } else {
                    o->dx = 0.0f;
                    o->dz = 0.0f;
                }
            }
            if (o->dx > SPEED_MAX) o->dx = SPEED_MAX;
            if (o->dx < -SPEED_MAX) o->dx = -SPEED_MAX;
            if (o->dz > SPEED_MAX) o->dz = SPEED_MAX;
            if (o->dz < -SPEED_MAX) o->dz = -SPEED_MAX;
            o->x += o->dx * dt;
            o->z += o->dz * dt;
            // Terry bounces at the camp perimeter.
            if (o->x < -CAMP_WIDTH / 2) { o->x = -CAMP_WIDTH / 2; o->dx = -o->dx; }
            if (o->x >  CAMP_WIDTH / 2) { o->x =  CAMP_WIDTH / 2; o->dx = -o->dx; }
            if (o->z < -CAMP_WIDTH)      { o->z = -CAMP_WIDTH;    o->dz = -o->dz; }
            if (o->z >  0.0f)            { o->z =  0.0f;          o->dz = -o->dz; }
        }

        // ---- Draw ----
        const int cx = 320, cy = 240;

        // Backdrop:
        //   sky    y = 0..96    LTCYAN  (~1/5 of the panel)
        //   grass  y = 96..480  GREEN   (~4/5 of the panel — camp plain
        //                                fills nearly the whole scene)
        // Terry's Camp.HC draws Mountain BI=1 at a far-perspective anchor
        // (z = -16*CAMP_WIDTH); on his monitor it renders as a distant
        // silhouette on the horizon. On badge it fills the sky and hides
        // the tents, so we skip it and leave the scene as plain grass +
        // sky per user's memory of Terry's rendering.
        const int camp_horizon = 96;
        gr_dc.color = C_LTCYAN;
        GrFillRect(&gr_dc, 0, 0, 640, camp_horizon);
        gr_dc.color = C_GREEN;
        GrFillRect(&gr_dc, 0, camp_horizon, 640, 480 - camp_horizon);

        // Ground plane grid — Terry draws 4 gray edges of the camp
        // rectangle in perspective at thickness 6. We do the same.
        camp_draw_t corners[4];
        float corner_x[4] = { -CAMP_WIDTH/2.0f,  CAMP_WIDTH/2.0f,
                              -CAMP_WIDTH/2.0f,  CAMP_WIDTH/2.0f };
        float corner_z[4] = { 0.0f,              0.0f,
                              -(float)CAMP_WIDTH, -(float)CAMP_WIDTH };
        int   corner_ok[4] = {0,0,0,0};
        for (int k = 0; k < 4; k++) {
            corner_ok[k] = camp_transform(corner_x[k], 0.0f, corner_z[k],
                                          vpx, vpy, vpz,
                                          rot_cos_x, rot_sin_x,
                                          cx, cy, &corners[k]);
        }
        gr_dc.color = C_LTGRAY;
        gr_dc.thick = 6;   // Terry's thickness for the perimeter
        int edges[4][2] = { {0,1}, {2,3}, {0,2}, {1,3} };
        for (int e = 0; e < 4; e++) {
            int a = edges[e][0], b = edges[e][1];
            if (corner_ok[a] && corner_ok[b])
                GrLine(&gr_dc,
                       corners[a].screen_x, corners[a].screen_y,
                       corners[b].screen_x, corners[b].screen_y);
        }
        gr_dc.thick = 1;

        // Transform all objects, collect the visible ones. Clip sprites
        // to screen_y >= 200 so nothing draws into the hills band.
        int draw_n = 0;
        for (int i = 0; i < CAMP_OBJS; i++) {
            camp_obj_t *o = &s_camp_objs[i];
            camp_draw_t d;
            if (!camp_transform(o->x, o->y, o->z, vpx, vpy, vpz,
                                rot_cos_x, rot_sin_x, cx, cy, &d))
                continue;
            if (d.screen_y < 200) continue;
            d.obj_index = i;
            s_camp_draws[draw_n++] = d;
        }
        // Fold the Golden Calf into the same list with obj_index=-1
        // so painter-sort places it correctly relative to people/tents.
        if (calf) {
            camp_draw_t d;
            if (camp_transform(0.0f, 0.0f, -CAMP_WIDTH / 2.0f,
                               vpx, vpy, vpz,
                               rot_cos_x, rot_sin_x,
                               cx, cy, &d)) {
                d.obj_index = -1;
                s_camp_draws[draw_n++] = d;
            }
        }

        // Sort back-to-front.
        for (int i = 1; i < draw_n; i++) {
            camp_draw_t t = s_camp_draws[i];
            int j = i - 1;
            while (j >= 0 && s_camp_draws[j].depth_z < t.depth_z) {
                s_camp_draws[j + 1] = s_camp_draws[j];
                j--;
            }
            s_camp_draws[j + 1] = t;
        }

        // Walking-animation frame (Terry: frame = 4 * Saw(tS, 0.5)).
        float saw = fmodf(tS, 0.5f) / 0.5f;
        int  fi   = (int)(4.0f * saw) & 3;
        const uint8_t *const right_frames[4] = {
            SPRITE_CAMP_BI_2, SPRITE_CAMP_BI_1,
            SPRITE_CAMP_BI_3, SPRITE_CAMP_BI_1,
        };
        const uint32_t right_sizes[4] = {
            SPRITE_CAMP_BI_2_SIZE, SPRITE_CAMP_BI_1_SIZE,
            SPRITE_CAMP_BI_3_SIZE, SPRITE_CAMP_BI_1_SIZE,
        };
        const uint8_t *const left_frames[4]  = {
            SPRITE_CAMP_BI_5, SPRITE_CAMP_BI_4,
            SPRITE_CAMP_BI_6, SPRITE_CAMP_BI_4,
        };
        const uint32_t left_sizes[4]  = {
            SPRITE_CAMP_BI_5_SIZE, SPRITE_CAMP_BI_4_SIZE,
            SPRITE_CAMP_BI_6_SIZE, SPRITE_CAMP_BI_4_SIZE,
        };

        for (int i = 0; i < draw_n; i++) {
            camp_draw_t *d = &s_camp_draws[i];
            float s = d->scale;
            if (s > 3.0f) s = 3.0f;

            if (d->obj_index == -1) {
                // Golden Calf — Terry's sprite is 56x33 Terry; clamp
                // 1.2..2.0 so it's a chunky centerpiece without eating
                // the frame. Depth-sorted with the crowd so people in
                // front of it correctly occlude.
                float cs = s;
                if (cs < 1.2f) cs = 1.2f;
                if (cs > 2.0f) cs = 2.0f;
                Sprite3S(&gr_dc, d->screen_x, d->screen_y, 0, cs,
                         SPRITE_CAMP_BI_8, SPRITE_CAMP_BI_8_SIZE);
                continue;
            }

            camp_obj_t *o = &s_camp_objs[d->obj_index];
            if (o->type == COT_PERSON) {
                int  idx = ((d->obj_index + fi) & 3);
                if (o->dx >= 0.0f)
                    Sprite3S(&gr_dc, d->screen_x, d->screen_y, 0, s,
                             right_frames[idx], right_sizes[idx]);
                else
                    Sprite3S(&gr_dc, d->screen_x, d->screen_y, 0, s,
                             left_frames[idx],  left_sizes[idx]);
            } else {  // COT_TENT
                // Terry's BI=7 tent bitmap is 107x123 Terry pixels —
                // way too detailed and slow to scale per-frame on the
                // ESP32-S3. Draw a small triangular tent silhouette
                // instead (matches Terry's Camp reference visually).
                int th_ = (int)(28.0f * s);
                if (th_ < 6)  th_ = 6;
                if (th_ > 36) th_ = 36;
                int tw_ = (th_ * 3) / 2;
                int bx  = d->screen_x - tw_ / 2;
                int by  = d->screen_y;
                gr_dc.color = C_LTGRAY;
                for (int dy = 0; dy < th_; dy++) {
                    int hw = tw_ * (th_ - dy) / (2 * th_);
                    GrFillRect(&gr_dc, d->screen_x - hw, by - dy, hw * 2, 1);
                }
                gr_dc.color = C_BLACK;
                GrLine(&gr_dc, bx,      by, d->screen_x, by - th_);
                GrLine(&gr_dc, bx + tw_, by, d->screen_x, by - th_);
                GrLine(&gr_dc, bx,      by, bx + tw_, by);
            }
        }

        // "!!GOLDEN CALF!!" blinking caption — still an overlay, but
        // now the calf sprite itself is depth-sorted with the crowd.
        if (calf && ((now / 125) & 1)) {
            gr_dc.color = C_LTRED;
            GrPrint(&gr_dc, cx - 60, cy - 80, "!!GOLDEN CALF!!");
        }

        // Chrome.
        gr_dc.color = C_BLACK;
        GrPrint(&gr_dc, 8,  8,  "BREAK CAMP");
        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 8,  464, forming ? "FORMING CIRCLE" : "WANDERING");
        GrPrint(&gr_dc, 448, 464, "A/BOOT EXIT");

        DCPresent(&gr_dc);
        (void)last_cycle;
        shrine_sleep_ms(30);
    }
}

// --- Mt Horeb scene ---
// Literal port of Terry's HorebA + HorebBSP + HorebC.
//   A wilderness scene: hundreds of pebbles + a mix of bushes, logs,
//   trees, sheep, and goats populated by weighted random type roll
//   (Terry's type_weights = {0, 30, 30, 15, 30, 3, 1, 1, 1}). Sheep
//   and goats drift on dx = 5*cos(0.5*t + theta), dz = 5*sin(...).
//   The player rotates view (LEFT/RIGHT) and moves fore/aft (UP/DOWN);
//   the goal is to find the Burning Bush (obj[0] forced to BUSH1;
//   distinguished only by the 45 random flame lines drawn around it).
//   When the player gets within 300 units, horeb_done fires.
//
//   View matrix: RotY(-alpha) then RotX(77deg). Projection: s = 100 /
//   (|z| + 50); screen_x = s*x + cx; screen_y = s*y + h.
//   Terry runs 256 obj + 4096 pebbles. We run 40 + 200 for perf.
//
//   Mountain backdrop is not part of this scene — Horeb's landscape
//   IS the mountain area.

typedef enum {
    HOT_PEBBLE = 0, HOT_BUSH1 = 1, HOT_BUSH2 = 2, HOT_LOG = 3,
    HOT_TREE1  = 4, HOT_TREE2 = 5, HOT_SHEEP = 6, HOT_GOAT1 = 7,
    HOT_GOAT2  = 8, HOT_TYPES = 9,
} horeb_type_t;

#define HOREB_OBJS       160   // Terry ran 256; badge cranked to 160 for density
#define HOREB_PEBBLES    250   // Terry ran 4096; badge keeps it low so the
                               // yellow sand reads flat (user preference)
#define HOREB_TOTAL      (HOREB_OBJS + HOREB_PEBBLES)
#define HOREB_BURNING    0     // obj[0] is Terry's O_BURNING_BUSH
#define HOREB_SPAWN_HALF 3000  // objects spawn uniformly in [-half, +half]
                               // world units. Narrower than Terry's 4096 so
                               // the scene reads dense right around the
                               // player's typical position instead of thin
                               // out at the horizon.

typedef struct {
    float          x, y, z;
    float          dx, dy, dz;
    float          theta;
    uint8_t        type;
    bool           sym;
} horeb_obj_t;

static horeb_obj_t s_horeb_objs[HOREB_TOTAL];

typedef struct {
    int   screen_x, screen_y;
    float depth_z;
    float scale;
    int   obj_index;
} horeb_draw_t;
static horeb_draw_t s_horeb_draws[HOREB_TOTAL];

// Terry's type_weights (index 0 unused for OBJs; pebbles bypass roll).
static const int HOREB_TYPE_WEIGHTS[HOT_TYPES] = {
    0, 30, 30, 15, 30, 3, 1, 1, 1
};

// Sprite table matching Terry's imgs[]. imgs[0] (pebble) is drawn as
// a pixel, not a sprite.
static const uint8_t *const HOREB_IMGS[HOT_TYPES] = {
    NULL,
    SPRITE_HOREBA_BI_1, SPRITE_HOREBA_BI_2, SPRITE_HOREBA_BI_3,
    SPRITE_HOREBA_BI_4, SPRITE_HOREBA_BI_5, SPRITE_HOREBA_BI_6,
    SPRITE_HOREBA_BI_7, SPRITE_HOREBA_BI_8,
};
static const uint32_t HOREB_SIZES[HOT_TYPES] = {
    0,
    SPRITE_HOREBA_BI_1_SIZE, SPRITE_HOREBA_BI_2_SIZE, SPRITE_HOREBA_BI_3_SIZE,
    SPRITE_HOREBA_BI_4_SIZE, SPRITE_HOREBA_BI_5_SIZE, SPRITE_HOREBA_BI_6_SIZE,
    SPRITE_HOREBA_BI_7_SIZE, SPRITE_HOREBA_BI_8_SIZE,
};

// Terry's pebble_colors[4].
static const color_t HOREB_PEBBLE_COLORS[4] = { C_BLACK, C_DKGRAY, C_DKGRAY, C_LTGRAY };

static void horeb_init(void)
{
    int total = 0;
    for (int i = 0; i < HOT_TYPES; i++) total += HOREB_TYPE_WEIGHTS[i];

    for (int i = 0; i < HOREB_TOTAL; i++) {
        horeb_obj_t *o = &s_horeb_objs[i];
        // Narrower than Terry's 4096-wide range so the scene reads dense
        // around the player's typical position (Terry ran 256 obj / 4096
        // px world; we run 160 obj / 3000 px world — comparable density).
        int span = HOREB_SPAWN_HALF * 2;
        o->x = (float)((int)shrine_god(span) - HOREB_SPAWN_HALF);
        o->y = 0.0f;
        o->z = (float)((int)shrine_god(span) - HOREB_SPAWN_HALF);
        o->theta = 6.28318f * (float)shrine_god(1000) / 1000.0f;
        o->sym = (bool)(shrine_god(2));
        o->dx = 0.0f; o->dy = 0.0f; o->dz = 0.0f;

        if (i < HOREB_OBJS) {
            // Weighted type roll — Terry's while(TRUE) { j-=weights[k]; ... }
            int j = (int)shrine_god(total);
            int k = 0;
            while (k < HOT_TYPES) {
                j -= HOREB_TYPE_WEIGHTS[k];
                if (j < 0) break;
                k++;
            }
            if (k >= HOT_TYPES) k = HOT_BUSH1;
            o->type = (uint8_t)k;
        } else {
            o->type = HOT_PEBBLE;
        }
    }
    // Terry: objs[O_BURNING_BUSH].type = OT_BUSH1;
    s_horeb_objs[HOREB_BURNING].type = HOT_BUSH1;
}

// Literal port of Terry's Mountain() in Mountain.HC — the "ascension"
// half of Terry's UpTheMountain flow that runs BEFORE the Horeb wander.
// Walker climbs up a set of zig-zag waypoints (Terry: BSpline2 through
// 17 control points; we linear-interp through the 9 unique waypoints
// which reads the same on our screen). Uses procedural brown triangle
// hill silhouettes (echoing the zig-zag path) + the walker frames
// extracted from Mountain.HC's tail. Terry's own Mountain BI=1 sprite
// is skipped — on badge it renders as a dense LTCYAN slab; user recalls
// his scene as simple hill silhouettes on a yellow desert. Returns true
// if the user aborted; false if the climb completed.
static bool scene_mountain(void)
{
    static const int WP_X[9] = {  0, 100, -100,  80, -80,  60, -60,  40, -37 };
    static const int WP_Y[9] = {  0, -20,  -40, -60, -60, -80,-100,-120,-140 };
    const int cx = 320, cy = 240;

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    const uint8_t *const R[4] = {
        SPRITE_MOUNTAIN_BI_2, SPRITE_MOUNTAIN_BI_3,
        SPRITE_MOUNTAIN_BI_4, SPRITE_MOUNTAIN_BI_3,
    };
    const uint32_t Rs[4] = {
        SPRITE_MOUNTAIN_BI_2_SIZE, SPRITE_MOUNTAIN_BI_3_SIZE,
        SPRITE_MOUNTAIN_BI_4_SIZE, SPRITE_MOUNTAIN_BI_3_SIZE,
    };
    const uint8_t *const L[4] = {
        SPRITE_MOUNTAIN_BI_5, SPRITE_MOUNTAIN_BI_6,
        SPRITE_MOUNTAIN_BI_5, SPRITE_MOUNTAIN_BI_7,
    };
    const uint32_t Ls[4] = {
        SPRITE_MOUNTAIN_BI_5_SIZE, SPRITE_MOUNTAIN_BI_6_SIZE,
        SPRITE_MOUNTAIN_BI_5_SIZE, SPRITE_MOUNTAIN_BI_7_SIZE,
    };

    int   wp_idx    = 0;      // current segment start index
    float t         = 0.0f;   // 0..1 along current segment
    const float step = 0.02f; // ~50 frames per segment @ 30ms = 1.5s / segment
    bool  last_left = false;
    int   x_last    = 0;

    while (wp_idx < 8) {
        shrine_input_scan();
        if (shrine_should_quit()) return true;    // BOOT exits everything
        if (shrine_key_pressed(BTN_A)) break;     // A skips into the wander

        uint32_t now = shrine_ms();

        int x = WP_X[wp_idx] + (int)((WP_X[wp_idx + 1] - WP_X[wp_idx]) * t);
        int y = WP_Y[wp_idx] + (int)((WP_Y[wp_idx + 1] - WP_Y[wp_idx]) * t);

        // Sky + desert seam. Terry's Mountain BI=1 sprite renders as a
        // dense LTCYAN slab on our badge; the user recalls his real
        // ascent scene as plain sky + yellow ground with simple zig-zag
        // hill silhouettes that mirror the walker's climbing path. We
        // paint those hill silhouettes as brown triangles below.
        const int horizon_y = 200;
        gr_dc.color = C_LTCYAN;
        GrFillRect(&gr_dc, 0, 0, 640, horizon_y);
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, horizon_y, 640, 480 - horizon_y);

        // Sun — Terry: brown outline + yellow flood at (50, 25) r=15.
        gr_dc.color = C_BROWN;
        GrCircle(&gr_dc, 50, 25, 15);
        gr_dc.color = C_YELLOW;
        GrFillCircle(&gr_dc, 50, 25, 14);

        // Simple hill silhouettes — two ridges of brown triangles rising
        // in front of the yellow desert. Filled by scanning each column
        // between the ridge endpoints and painting from the ridge line
        // down to the horizon, so they read as solid hills instead of
        // just outlines against the split yellow/blue background. Not
        // Terry's mountain bitmap; matches user's memory of the scene as
        // "simple drawings of hills, similar to the zig-zag path he
        // ascends."
        static const int RIDGE_BACK_X[]  = {  30, 220, 420, 610, 640 };
        static const int RIDGE_BACK_Y[]  = {   0, -90, -40, -70, -30 };
        static const int RIDGE_FRONT_X[] = {  80, 300, 500, 640 };
        static const int RIDGE_FRONT_Y[] = {   0, -130, -60, -20 };

        // Back ridge — darker brown so the front ridge stands out.
        gr_dc.color = C_BROWN;
        for (int seg = 0; seg + 1 < (int)(sizeof(RIDGE_BACK_X)/sizeof(int)); seg++) {
            int x0 = RIDGE_BACK_X[seg],     y0 = horizon_y + RIDGE_BACK_Y[seg];
            int x1 = RIDGE_BACK_X[seg + 1], y1 = horizon_y + RIDGE_BACK_Y[seg + 1];
            if (x1 <= x0) continue;
            for (int x = x0; x <= x1; x++) {
                int y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
                if (y < horizon_y) {
                    GrFillRect(&gr_dc, x, y, 1, horizon_y - y);
                }
            }
        }

        // Front ridge — YELLOW fill with brown outline so it reads as a
        // near hill in front of the darker back ridge. Same fill logic.
        gr_dc.color = C_YELLOW;
        for (int seg = 0; seg + 1 < (int)(sizeof(RIDGE_FRONT_X)/sizeof(int)); seg++) {
            int x0 = RIDGE_FRONT_X[seg],     y0 = horizon_y + RIDGE_FRONT_Y[seg];
            int x1 = RIDGE_FRONT_X[seg + 1], y1 = horizon_y + RIDGE_FRONT_Y[seg + 1];
            if (x1 <= x0) continue;
            for (int x = x0; x <= x1; x++) {
                int y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
                if (y < horizon_y) {
                    GrFillRect(&gr_dc, x, y, 1, horizon_y - y);
                }
            }
        }
        // Outline the front ridge in brown so it separates from the
        // yellow desert below and reads as a distinct hill shape.
        gr_dc.color = C_BROWN;
        gr_dc.thick = 2;
        for (int seg = 0; seg + 1 < (int)(sizeof(RIDGE_FRONT_X)/sizeof(int)); seg++) {
            GrLine(&gr_dc,
                   RIDGE_FRONT_X[seg],     horizon_y + RIDGE_FRONT_Y[seg],
                   RIDGE_FRONT_X[seg + 1], horizon_y + RIDGE_FRONT_Y[seg + 1]);
        }
        gr_dc.thick = 1;

        // Trail — Terry draws 8 brown line segments between waypoints.
        gr_dc.color = C_BROWN;
        gr_dc.thick = 2;
        for (int i = 0; i < 8; i++) {
            GrLine(&gr_dc, cx + WP_X[i],     cy + WP_Y[i],
                           cx + WP_X[i + 1], cy + WP_Y[i + 1]);
        }
        gr_dc.thick = 1;

        // Blink "Mt. Horeb" label.
        if ((now / 500) & 1) {
            gr_dc.color = C_BLACK;
            GrPrint(&gr_dc, cx - 36, 80, "MT. HOREB");
        }

        // Walker direction from last x — Terry's `if (x<last_x) last_left`.
        if (x < x_last)      last_left = true;
        else if (x > x_last) last_left = false;

        int fi = (int)((now / 167) & 3);
        if (last_left)
            Sprite3(&gr_dc, cx + x, cy + y, 0, L[fi], Ls[fi]);
        else
            Sprite3(&gr_dc, cx + x, cy + y, 0, R[fi], Rs[fi]);

        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 8,   464, "ASCENDING MT. HOREB");
        GrPrint(&gr_dc, 496, 464, "A SKIP");

        DCPresent(&gr_dc);
        x_last = x;
        t += step;
        if (t >= 1.0f) { t = 0.0f; wp_idx++; }
        shrine_sleep_ms(30);
    }
    return false;
}

static void scene_horeb(void)
{
    horeb_init();

    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    // Pre-wrap Exodus 3:1 verse.
    verse_line_t vlines[VERSE_MAX_LINES];
    int vcount = 0;
    const kjv_verse_t *kv = kjv_lookup("EXODUS", 3, 1);
    if (kv) vcount = wrap_lines(kv->text, 37, vlines, VERSE_MAX_LINES);

    // Player view state — Terry's vx, vz, alpha.
    float vx     = 0.0f;
    float vz     = 0.0f;
    float alpha  = 0.0f;
    bool  done   = false;

    uint32_t t0     = shrine_ms();
    uint32_t t_last = 0;
    uint32_t frame_i = 0;

    while (!done) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;

        // Controls: LEFT/RIGHT rotate, UP/DOWN move.
        if (shrine_key_held(BTN_LEFT))  alpha -= 0.05f;
        if (shrine_key_held(BTN_RIGHT)) alpha += 0.05f;
        if (shrine_key_held(BTN_UP))    { vx -= 8.0f * sinf(alpha); vz -= 8.0f * cosf(alpha); }
        if (shrine_key_held(BTN_DOWN))  { vx += 8.0f * sinf(alpha); vz += 8.0f * cosf(alpha); }

        uint32_t now = shrine_ms();
        float    tS  = (float)(now - t0) / 1000.0f;
        float    dt  = t_last ? (float)(now - t_last) / 1000.0f : 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        t_last = now;
        frame_i++;

        // Terry's AnimateTask — sheep/goats orbit around their theta.
        for (int i = 0; i < HOREB_TOTAL; i++) {
            horeb_obj_t *o = &s_horeb_objs[i];
            if (o->type == HOT_SHEEP || o->type == HOT_GOAT1 || o->type == HOT_GOAT2) {
                o->x += o->dx * dt;
                o->z += o->dz * dt;
                o->dx = 5.0f * cosf(0.5f * tS + o->theta);
                o->dz = 5.0f * sinf(0.5f * tS + o->theta);
            }
        }

        // View matrix: RotY(-alpha), then RotX(77°). We compute both
        // rotations inline — same 6 trig calls as full Mat4x4 mult.
        float cy_    = cosf(-alpha);
        float sy_    = sinf(-alpha);
        const float rot_x_deg = 77.0f * 3.14159265f / 180.0f;
        float cx_    = cosf(rot_x_deg);
        float sx_    = sinf(rot_x_deg);
        // Screen bands — Terry's Horeb (post-ascent) is an open desert
        // with NO mountain silhouette. The mountain belongs to the
        // scene_mountain ascension only. Here we want just sky + sand.
        //   sky     y = 0..60   LTCYAN (thin — 77° tilt means little sky)
        //   ground  y = 60..440 YELLOW desert
        //   hud     y = 440..480 yellow HUD strip
        const int scr_cx        = 320;
        const int scr_h         = 440;
        const int HOREB_HORIZON = 60;

        gr_dc.color = C_LTCYAN;
        GrFillRect(&gr_dc, 0, 0, 640, HOREB_HORIZON);
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, HOREB_HORIZON, 640, 480 - HOREB_HORIZON);

        // Thin brown horizon band where the ground plane's vanishing
        // point sits. Not hills — just a distance cue.
        gr_dc.color = C_BROWN;
        GrFillRect(&gr_dc, 0, HOREB_HORIZON, 640, 3);

        // Distant dune silhouettes along the horizon — a few soft brown
        // humps pinned to the sky/desert seam so they always sit at the
        // horizon regardless of camera motion. Purely visual (not part
        // of the world objects). Gives the eye something to lock onto
        // besides empty sand while keeping the ground itself flat per
        // user request (no mid-ground pebble noise, no grass tufts).
        gr_dc.color = C_BROWN;
        for (int d = 0; d < 6; d++) {
            int cx  = 40 + d * 110;
            int rw  = 60 + ((d * 17) % 40);
            int rh  = 8  + ((d * 11) % 6);
            for (int dy_ = 0; dy_ < rh; dy_++) {
                float ny = (float)dy_ / (float)rh;
                int half = (int)(rw * sqrtf(1.0f - ny * ny)) / 2;
                if (half > 0) {
                    GrFillRect(&gr_dc,
                               cx - half, HOREB_HORIZON + 3 - dy_,
                               half * 2, 1);
                }
            }
        }

        // Verse in blue on the sky strip — first 8 seconds only.
        if (tS < 8.0f) {
            gr_dc.color = C_BLUE;
            draw_wrapped_slice(&gr_dc, 8, 8, 18, vlines, vcount, 0,
                               vcount < 2 ? vcount : 2);
        }

        // Sun — Terry: transform (vx, 0, 1000000+vz), scale s=100/(|z|+50),
        //             if y<0 draw brown circle + yellow flood at
        //             (s*x+cx, 15).
        {
            float sx0 = (float)vx;
            float sy0 = 0.0f;
            float sz0 = 1000000.0f + (float)vz;
            // RotY(-alpha)
            float rx = cy_ * sx0 + sy_ * sz0;
            float rz = -sy_ * sx0 + cy_ * sz0;
            // RotX(77°)
            float ry = cx_ * sy0 - sx_ * rz;
            rz       = sx_ * sy0 + cx_ * rz;
            if (ry < 0.0f) {
                float s = 100.0f / (fabsf(rz) + 50.0f);
                int   sxp = (int)(s * rx) + scr_cx;
                gr_dc.color = C_BROWN;
                GrCircle(&gr_dc, sxp, 15, 15);
                gr_dc.color = C_YELLOW;
                GrFloodFill(&gr_dc, sxp, 15);
            }
        }

        // Transform all objects.
        int burning_screen_x = -9999, burning_screen_y = -9999;
        float burning_scale  = 0.0f;
        int draw_n = 0;
        for (int i = 0; i < HOREB_TOTAL; i++) {
            horeb_obj_t *o = &s_horeb_objs[i];
            // Terry: x1 = x + vx; y1 = y; z1 = z + vz; then MulXYZ.
            float x1 = o->x + vx;
            float y1 = o->y;
            float z1 = o->z + vz;
            // RotY(-alpha)
            float rx = cy_ * x1 + sy_ * z1;
            float rz = -sy_ * x1 + cy_ * z1;
            // RotX(77°)
            float ry = cx_ * y1 - sx_ * rz;
            rz       = sx_ * y1 + cx_ * rz;
            // Skip if behind the camera (Terry: if (tmpo->z1<0) break).
            if (rz < 0.0f) continue;
            float s   = 100.0f / (fabsf(rz) + 50.0f);
            int   sxp = (int)(s * rx) + scr_cx;
            int   syp = (int)(s * ry) + scr_h;

            // Sheep/goat symmetry — Terry: check dx post-transform for
            // direction flag.
            if (o->type == HOT_SHEEP || o->type == HOT_GOAT1 || o->type == HOT_GOAT2) {
                float ddx = cy_ * o->dx + sy_ * o->dz;
                o->sym = (ddx < 0.0f);
            }

            // Clip sprites to below the horizon — objects that would
            // render into the sky strip (y < HOREB_HORIZON) are so far
            // away they'd just be single-pixel dots up there, and they
            // look "floating" against the sky. Skip them.
            if (syp < HOREB_HORIZON + 4) continue;

            s_horeb_draws[draw_n].screen_x  = sxp;
            s_horeb_draws[draw_n].screen_y  = syp;
            s_horeb_draws[draw_n].depth_z   = rz;
            s_horeb_draws[draw_n].scale     = s * 2.0f;    // Terry: Mat4x4Scale(dc->r, s*2)
            s_horeb_draws[draw_n].obj_index = i;
            draw_n++;

            if (i == HOREB_BURNING) {
                burning_screen_x = sxp;
                burning_screen_y = syp;
                burning_scale    = s;
                // Terry's win condition: sqr(s*x) + sqr(s*y) < 300*300
                float bx = s * rx;
                float by = s * ry;
                if (bx * bx + by * by < 300.0f * 300.0f)
                    done = true;
            }
        }

        // Sort back-to-front (larger depth first).
        for (int i = 1; i < draw_n; i++) {
            horeb_draw_t t = s_horeb_draws[i];
            int j = i - 1;
            while (j >= 0 && s_horeb_draws[j].depth_z < t.depth_z) {
                s_horeb_draws[j + 1] = s_horeb_draws[j];
                j--;
            }
            s_horeb_draws[j + 1] = t;
        }

        // Draw objects.
        for (int i = 0; i < draw_n; i++) {
            horeb_draw_t *d = &s_horeb_draws[i];
            horeb_obj_t  *o = &s_horeb_objs[d->obj_index];
            if (o->type == HOT_PEBBLE) {
                gr_dc.color = HOREB_PEBBLE_COLORS[d->obj_index & 3];
                GrPlot(&gr_dc, d->screen_x, d->screen_y);
            } else {
                float s = d->scale;
                if (s > 4.0f) s = 4.0f;
                if (s < 0.05f) continue;   // vanishingly small
                // Ground shadow — small dark ellipse at the sprite's
                // anchor foot, sized proportional to the sprite scale.
                // Grounds the figure so it doesn't read as floating.
                int shadow_w = (int)(24.0f * s);
                int shadow_h = (int)(6.0f * s);
                if (shadow_w < 4) shadow_w = 4;
                if (shadow_h < 2) shadow_h = 2;
                gr_dc.color = C_BROWN;
                for (int sy_off = -shadow_h; sy_off <= shadow_h; sy_off++) {
                    float ny = (float)sy_off / (float)shadow_h;
                    int   half = (int)(shadow_w * sqrtf(1.0f - ny * ny));
                    if (half > 0) {
                        GrFillRect(&gr_dc,
                                   d->screen_x - half, d->screen_y + sy_off,
                                   half * 2, 1);
                    }
                }
                Sprite3S(&gr_dc, d->screen_x, d->screen_y, 0, s,
                         HOREB_IMGS[o->type], HOREB_SIZES[o->type]);
            }
        }

        // Burning-bush effect — Terry draws 45 random line segments
        // within ~20 units of the bush, color cycles per frame.
        if (burning_scale > 0.02f) {
            gr_dc.color = (color_t)(frame_i & 15);
            int radius = (int)(20.0f * burning_scale * 2.0f);
            if (radius < 3) radius = 3;
            if (radius > 40) radius = 40;
            for (int k = 0; k < 30; k++) {
                float m1 = (float)shrine_god(1000) / 1000.0f;
                float a1 = 6.28318f * (float)shrine_god(1000) / 1000.0f;
                float m2 = (float)shrine_god(1000) / 1000.0f;
                float a2 = 6.28318f * (float)shrine_god(1000) / 1000.0f;
                m1 *= m1; m2 *= m2;
                int x1 = burning_screen_x + (int)(radius * m1 * cosf(a1));
                int y1 = burning_screen_y - 25 + (int)(radius * m1 * sinf(a1));
                int x2 = burning_screen_x + (int)(radius * m2 * cosf(a2));
                int y2 = burning_screen_y - 25 + (int)(radius * m2 * sinf(a2));
                GrLine(&gr_dc, x1, y1, x2, y2);
            }
        }

        // Dedicated bottom HUD strip — Terry-color yellow bg with a
        // black separator line. Nothing gameside renders below y=440
        // because the projection uses h=440 above.
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, 440, 640, 40);
        gr_dc.color = C_BLACK;
        gr_dc.thick = 2;
        GrLine(&gr_dc, 0, 440, 640, 440);
        gr_dc.thick = 1;
        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 8, 456, "LR ROTATE  UD MOVE");
        GrPrint(&gr_dc, 552, 456, "A EXIT");
        // "Find the Burning Bush" blinks in the middle of the strip
        // (Terry originally floats it dead-center of the doc; we keep
        // it in the HUD so it never obscures the desert).
        if ((now / 500) & 1) {
            gr_dc.color = C_LTRED;
            GrPrint(&gr_dc, scr_cx - 84, 456, "FIND THE BURNING BUSH");
        }

        DCPresent(&gr_dc);
        shrine_sleep_ms(30);
    }

    // Win screen — Terry just exits back to the menu.
    gr_dc.color = C_YELLOW;
    GrFillRect(&gr_dc, 0, 0, 640, 480);
    gr_dc.color = C_LTRED;
    GrPrint(&gr_dc, 200, 200, "YOU FOUND IT");
    gr_dc.color = C_BLUE;
    GrPrint(&gr_dc, 128, 240, "\"TAKE OFF YOUR SANDALS.\"");
    GrPrint(&gr_dc, 240, 260, "-- EXODUS 3:5");
    gr_dc.color = C_DKGRAY;
    GrPrint(&gr_dc, 200, 400, "A OR BOOT TO RETURN");
    DCPresent(&gr_dc);
    shrine_beep(1600, 100); shrine_beep(2000, 100); shrine_beep(2400, 200);
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;
        shrine_sleep_ms(30);
    }
}

// --- Moses Comics scene ---
// Terry's ViewComics reads Comics/*.DD.Z from the TempleOS ISO's
// AfterEgypt directory. The real comic files are DolDoc documents
// (markup text + optional embedded sprite tail per comic). We fetched
// the first batch from the canewsin/templeos-1 ISO dump and generate
// `sprite_comics.h` at build-prep time via tools/build_comics.py; the
// scene renders the DolDoc directly ($FG,N$ color changes, $WW,1$
// word wrap toggle, $SP,"",BI=1$ inline sprite anchor, newlines).

#include "sprite_comics.h"

// Walk a DolDoc-markup text and stamp glyphs onto gr_dc. Handles the
// small tag set Terry's Moses comics actually use: $FG,N$ (foreground
// color 0..15), $WW,N$ (word wrap toggle — we always wrap on newline
// so this is a no-op), $SP,"...",BI=N$ (inline sprite anchor — draws
// the comic's associated sprite at the current cursor, if present).
// Terry's doc default text attr is YELLOW bg + BLUE fg; caller sets
// the background rectangle before calling this.
static void render_comic_doldoc(int x0, int y0,
                                const char *text,
                                const uint8_t *sprite, unsigned sprite_size)
{
    int cx = x0, cy = y0;
    color_t color = C_BLUE;
    const int line_h = 18;
    // GrPrint stamps an 8 fb-px glyph = 16 Terry px per char. Wrap
    // just before the right edge so Terry's own long lines (which
    // assume ~80 chars on his 640x480 doc) don't run off our narrower
    // fb frame at 40 chars.
    const int max_x = 632;

    while (*text) {
        if (*text == '$') {
            // Peek the tag name — first 2-3 chars after '$'.
            const char *p = text + 1;
            if (p[0] == 'F' && p[1] == 'G') {
                // $FG,N$ — color change (N is 0..15 palette index).
                p += 2;
                if (*p == ',') p++;
                int n = 0;
                while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
                if (*p == '$') p++;
                color = (color_t)(n & 15);
                text = p;
                continue;
            }
            if (p[0] == 'W' && p[1] == 'W') {
                // $WW,N$ — word wrap toggle, no-op for us.
                while (*p && *p != '$') p++;
                if (*p) p++;
                text = p;
                continue;
            }
            if (p[0] == 'S' && p[1] == 'P') {
                // $SP,"...",BI=N$ — inline sprite anchor. Draw the
                // comic's sprite at the current cursor if available.
                while (*p && *p != '$') p++;
                if (*p) p++;
                if (sprite && sprite_size > 0) {
                    Sprite3(&gr_dc, cx, cy, 0, sprite, sprite_size);
                }
                text = p;
                // Sprite tags in Terry's comics are followed by lots
                // of blank space to reserve room for the drawing; the
                // newlines in the source push us past the sprite area.
                continue;
            }
            // Unknown tag — skip to next '$' (or end).
            const char *q = text + 1;
            while (*q && *q != '$') q++;
            if (*q) q++;
            text = q;
            continue;
        }
        if (*text == '\n') {
            cx = x0;
            cy += line_h;
            text++;
            continue;
        }
        if (*text == '\r') { text++; continue; }
        // Regular glyph — GrPrint advances 8 fb (16 Terry) per char.
        // Wrap at max_x so long Terry lines don't drop off the right.
        if (cx > max_x - 16) {
            cx = x0;
            cy += line_h;
            // Skip a leading space at the start of the wrapped line
            // (Terry's text has lots of indent spaces we'd otherwise
            // stack against the left margin).
            if (*text == ' ') { text++; continue; }
        }
        char one[2] = { *text, 0 };
        gr_dc.color = color;
        GrPrint(&gr_dc, cx, cy, one);
        cx += 16;
        text++;
    }
}

static void scene_comics(void)
{
    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);
    int page = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) return;
        if (shrine_key_pressed(BTN_LEFT))  page = (page - 1 + MOSES_COMICS_N) % MOSES_COMICS_N;
        if (shrine_key_pressed(BTN_RIGHT)) page = (page + 1) % MOSES_COMICS_N;

        // Terry's doc attr for Moses comics is YELLOW bg + BLUE fg.
        gr_dc.color = C_YELLOW;
        GrFillRect(&gr_dc, 0, 0, 640, 480);

        // Render the comic's DolDoc text + inline sprite.
        render_comic_doldoc(16, 16,
                            MOSES_COMICS[page].text,
                            MOSES_COMICS[page].sprite,
                            MOSES_COMICS[page].sprite_size);

        // Page indicator + nav hints in the bottom-right, drawn last
        // so they sit on top of the doc.
        char buf[24];
        snprintf(buf, sizeof(buf), "%s   %d/%d",
                 MOSES_COMICS[page].name, page + 1, MOSES_COMICS_N);
        gr_dc.color = C_DKGRAY;
        GrPrint(&gr_dc, 8, 464, buf);
        GrPrint(&gr_dc, 448, 464, "LR PAGE  A EXIT");

        DCPresent(&gr_dc);
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

// --- Splash trailer ---
// Literal port of Terry's Trailer() in AfterEgypt.HC. Terry starts by
// showing AESplash.DD (an uncompressed DolDoc image we don't have on
// this side of the LZW gate) then flashes four dramatic messages in a
// black/red blinking bordered box at the bottom (Terry: TMsg() with
// GrRect black/red border at (0, GR_HEIGHT-FONT_HEIGHT*3, GR_WIDTH,
// FONT_HEIGHT*2), Blink(5) toggle rate, yellow centered text, 1.5s).
// We compose an atmospheric backdrop using our own extracted sprites
// (burning bush + Moses figure) since the splash image isn't shippable.

static const char *TRAILER_MSGS[4] = {
    "LEAVING ALL BEHIND, THEY FLED.",
    "FOUND THEMSELVES IN A DESERT.",
    "GOD!  WE'RE GONNA DIE!",
    "\"TRUST ME!\"",
};
#define TRAILER_MSG_MS 1500     // Terry: while (tS-t0 < 1.5)

// Draw Terry's After Egypt scale+sword emblem. Reproduces the tile
// icon from Terry's AfterEgypt.HC ($SP+PU," Egypt") as a procedural
// composition — dark-blue framed cyan panel with a yellow scale wedge
// suspending two hanging pans and a chunky diagonal silver sword with
// a yellow crossguard, per the reference art the user shared.
// All coords are Terry-space (640x480); half=~200 gives a large centered
// emblem, half=~50 gives the small header logo used by the main splash.
static void draw_ae_emblem_grdc(int cx, int cy, int half)
{
    int x0 = cx - half, y0 = cy - half;
    int size = half * 2;
    int b    = size / 10; if (b < 2) b = 2;

    // Dark-blue frame + cyan interior.
    gr_dc.color = C_BLUE;
    GrFillRect(&gr_dc, x0, y0, size, size);
    gr_dc.color = C_LTCYAN;
    GrFillRect(&gr_dc, x0 + b, y0 + b, size - 2 * b, size - 2 * b);

    // ---- Sword (drawn under the scale so pans hang cleanly in front)
    // Diagonal from lower-left handle to upper-right tip.
    int hx = cx - half * 3 / 8;
    int hy = cy + half * 3 / 4;
    int tx = cx + half * 3 / 4;
    int ty = cy - half * 3 / 4;
    // Stacked lines for a beveled silver look.
    gr_dc.color = C_BLACK;   gr_dc.thick = 12; GrLine(&gr_dc, hx, hy, tx, ty);
    gr_dc.color = C_WHITE;   gr_dc.thick =  9; GrLine(&gr_dc, hx, hy, tx, ty);
    gr_dc.color = C_LTGRAY;  gr_dc.thick =  6; GrLine(&gr_dc, hx, hy, tx, ty);
    gr_dc.color = C_WHITE;   gr_dc.thick =  2; GrLine(&gr_dc, hx, hy, tx, ty);
    gr_dc.thick = 1;

    // Yellow crossguard just above the handle, drawn as an axis-aligned
    // slab for readability at this scale.
    int gx = hx + (tx - hx) / 8;
    int gy = hy + (ty - hy) / 8;
    int gw = size / 10 + 4;
    int gh = size / 24 + 4;
    gr_dc.color = C_BLACK;
    GrFillRect(&gr_dc, gx - gw / 2 - 2, gy - gh / 2 - 2, gw + 4, gh + 4);
    gr_dc.color = C_YELLOW;
    GrFillRect(&gr_dc, gx - gw / 2,     gy - gh / 2,     gw,     gh);

    // ---- Scale (upper-center) ----
    // Yellow triangular wedge above a horizontal beam.
    int apex_x = cx;
    int apex_y = cy - half * 3 / 4;
    int base_y = cy - half * 1 / 3;
    int half_base = half / 2;
    int slices = base_y - apex_y;
    gr_dc.color = C_YELLOW;
    for (int i = 0; i < slices; i++) {
        int hw = half_base * i / slices;
        GrFillRect(&gr_dc, apex_x - hw, apex_y + i, hw * 2, 1);
    }
    gr_dc.color = C_BLACK;
    GrLine(&gr_dc, apex_x, apex_y, apex_x - half_base, base_y);
    GrLine(&gr_dc, apex_x, apex_y, apex_x + half_base, base_y);
    GrLine(&gr_dc, apex_x - half_base, base_y, apex_x + half_base, base_y);

    // Horizontal beam a hair below the wedge.
    int beam_x1 = cx - half_base - 8;
    int beam_x2 = cx + half_base + 8;
    int beam_y  = base_y + 4;
    gr_dc.color = C_BLACK;
    gr_dc.thick = 3;
    GrLine(&gr_dc, beam_x1, beam_y, beam_x2, beam_y);
    gr_dc.thick = 1;

    // Two hanging pans — cone outlines with yellow bottoms.
    int pan_off_x   = half_base - 5;
    int pan_top_y   = beam_y + 2;
    int pan_bot_y   = beam_y + half / 3;
    int pan_half_w  = size / 14;
    for (int side = -1; side <= 1; side += 2) {
        int px = cx + side * pan_off_x;
        // Rope from beam to pan
        gr_dc.color = C_BLACK;
        gr_dc.thick = 2;
        GrLine(&gr_dc, px, beam_y, px - pan_half_w, pan_bot_y);
        GrLine(&gr_dc, px, beam_y, px + pan_half_w, pan_bot_y);
        GrLine(&gr_dc, px - pan_half_w, pan_bot_y,
                       px + pan_half_w, pan_bot_y);
        gr_dc.thick = 1;
        // Yellow bottom of the pan (thin band inside the cone).
        gr_dc.color = C_YELLOW;
        int band_h = pan_half_w > 6 ? 4 : 2;
        for (int dy = 0; dy < band_h; dy++) {
            int hw = pan_half_w - dy - 1;
            if (hw < 1) continue;
            GrFillRect(&gr_dc, px - hw, pan_bot_y - band_h + dy,
                       hw * 2, 1);
        }
    }
}

// AESplash backdrop — plain LTCYAN sky + YELLOW ground.
//
// We do have Terry's extracted AESplash bitmap (canewsin aiwnios dump,
// SPT_BITMAP 640x589), but it contains an elaborate mountain scene that
// doesn't match the user's memory of Terry's actual splash rendering —
// they recall it as classic Terry: yellow ground, blue sky, four TMsg
// lines on top. So we paint the two rects manually and skip the sprite.
static void trailer_paint_backdrop(uint32_t now)
{
    (void)now;
    // Terry's canvas is 640x480. Horizon around 3/8 of the way down so
    // there's more sky than ground — reads as "wilderness sky over
    // desert" at a glance.
    const int horizon = 180;
    gr_dc.color = C_LTCYAN;
    GrFillRect(&gr_dc, 0, 0, 640, horizon);
    gr_dc.color = C_YELLOW;
    GrFillRect(&gr_dc, 0, horizon, 640, 480 - horizon);
}

// Terry's TMsg — 16 Terry-px tall bordered box, blinking red/black
// outline, yellow text centered, exit early on any button.
static void trailer_paint_msg(const char *msg, uint32_t now)
{
    // Border box at Terry (0, 456, 640, 16) — Terry: GR_HEIGHT-FONT*3=456.
    bool blink = ((now / 100) & 1);
    gr_dc.color = blink ? C_LTRED : C_BLACK;
    GrFillRect(&gr_dc, 0, 456, 640, 16);
    gr_dc.color = C_BLACK;
    GrFillRect(&gr_dc, 4, 458, 632, 12);
    // Yellow text centered — glyph is 8 fb = 16 Terry pixels wide.
    int nlen = 0; while (msg[nlen]) nlen++;
    int text_x = (640 - nlen * 16) / 2;
    gr_dc.color = C_YELLOW;
    GrPrint(&gr_dc, text_x, 460, msg);
}

// Returns true if the user chose to skip (or held BOOT to bail out).
// Ignores input for the first 400 ms — otherwise the A press that
// selected "After Egypt" from the parent menu will immediately skip
// the trailer we just entered.
static bool scene_splash(void)
{
    CDCInit(g_scene_fb, SCREEN_W, SCREEN_H, 2);

    // Show initial backdrop for ~500ms — Terry: Sleep(500) after Type().
    uint32_t entered = shrine_ms();
    while (shrine_ms() - entered < 500) {
        shrine_input_scan();
        if (shrine_should_quit()) return true;
        trailer_paint_backdrop(shrine_ms());
        DCPresent(&gr_dc);
        shrine_sleep_ms(30);
    }

    // Then the four messages, each 1.5s. Key press skips all remaining,
    // but only after we've settled past the parent menu's A press.
    for (int m = 0; m < 4; m++) {
        uint32_t t0 = shrine_ms();
        while (shrine_ms() - t0 < TRAILER_MSG_MS) {
            shrine_input_scan();
            if (shrine_should_quit()) return true;
            if (m > 0 || (shrine_ms() - t0) > 400) {
                if (shrine_key_pressed(BTN_A) || shrine_key_pressed(BTN_B))
                    return true;
            }
            uint32_t now = shrine_ms();
            trailer_paint_backdrop(now);
            trailer_paint_msg(TRAILER_MSGS[m], now);
            DCPresent(&gr_dc);
            shrine_sleep_ms(30);
        }
    }
    return false;
}

// --- Entry point ---
void game_afteregypt_run(void)
{
    // Terry's Trailer() runs before the main TakeTurn() loop.
    scene_splash();

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
            case SC_MAP:    scene_map();   break;
            case SC_CAMP:   scene_camp();  break;
            case SC_WATER:  scene_water();  break;
            case SC_BATTLE: scene_battle(); break;
            case SC_QUAIL:  scene_quail();  break;
            case SC_HOREB:
                // Terry's UpTheMountain calls Mountain(); Horeb();
                // in sequence — ascend, then wander to find the bush.
                if (!scene_mountain())
                    scene_horeb();
                break;
            case SC_COMICS: scene_comics(); break;
            default: break;
            }
            draw_menu();
        }
        shrine_sleep_ms(30);
    }
}
