// game_holyc.c — TempleOS-style HolyC REPL cameo.
//
// The badge has 7 buttons and no keyboard, so this isn't a real REPL —
// it's a curated command palette that looks and feels like Terry's
// TempleOS terminal:
//   - Yellow background + BLUE text (Terry's Fs->text_attr default)
//   - Blinking underscore cursor at the prompt
//   - Faint CRT scanlines painted every 3rd row (near-yellow) so the
//     screen has Terry's monitor texture
//   - Occasional "static" flash (single-frame all-yellow blip) every
//     few seconds, evoking Terry's monitor calibration wobble
//
// UI:
//   UP / DOWN     select a command
//   A             run the highlighted command (output appears below)
//   B             clear the terminal
//   BOOT          exit
//
// Commands are Terry's canonical HolyC one-liners — small enough to fit
// on-screen, with outputs faithful to what TempleOS would print.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"
#include "vocab.h"

#include "scene_fb.h"
#define s_fb g_scene_fb

#include <stdio.h>
#include <string.h>

// ---- fb helpers (same pattern as other scenes) ----
static inline uint16_t rgb(color_t c) { return PAL_RGB565[c & 15]; }
static inline void fb_pixel(int x, int y, color_t c)
{
    if ((unsigned)x < (unsigned)SCREEN_W && (unsigned)y < (unsigned)SCREEN_H)
        s_fb[y * SCREEN_W + x] = rgb(c);
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
static void fb_putc(int x, int y, char ch, color_t fg, color_t bg)
{
    uint8_t code = (uint8_t)ch;
    if (code >= 128) code = ' ';
    const uint8_t *g = FONT8X8[code];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++)
            fb_pixel(x + col, y + row, (bits & (1 << col)) ? fg : bg);
    }
}
static void fb_puts_px(int x, int y, const char *s, color_t fg, color_t bg)
{
    while (*s) { fb_putc(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static void fb_puts(int col, int row, const char *s, color_t fg, color_t bg)
{
    fb_puts_px(col * GLYPH_W, row * GLYPH_H, s, fg, bg);
}
static inline void fb_clear(color_t c)
{
    uint16_t v = rgb(c);
    int n = SCREEN_W * SCREEN_H;
    for (int i = 0; i < n; i++) s_fb[i] = v;
}

// ---- Content ----

typedef struct {
    const char *cmd;    // what the user "types"
    // Up to 6 output lines; empty strings terminate the list.
    const char *out[6];
} holyc_cmd_t;

// GodWord output cannot be hardcoded (Terry's RNG picks new each time),
// so we mark it with a sentinel and inject at run time.
#define OUT_GODWORD  "\x01"

static const holyc_cmd_t COMMANDS[] = {
    {
        "God;",
        { "Ask God for a word...",
          OUT_GODWORD,
          "", "", "", "" }
    },
    {
        "1+1;",
        { "0x0000000000000002",
          "", "", "", "", "" }
    },
    {
        "PopUpOk(\"HELLO\");",
        { "+--- Ok ------------+",
          "|    HELLO          |",
          "|                   |",
          "|      [ OK ]       |",
          "+-------------------+",
          "" }
    },
    {
        "Snd(1000);",
        { "*beep*", "", "", "", "", "" }
    },
    {
        "AdamBomb;",
        { "AdamBomb: not on this hardware.",
          "(Would page-fault all rings.)",
          "", "", "", "" }
    },
    {
        "Dir;",
        { "  Home/",
          "  Doc/",
          "  Adam/",
          "  Kernel/",
          "  Demo/AfterEgypt.HC",
          "  Bible.TXT" }
    },
    {
        "Type(\"Bible.TXT\",1);",
        { "Gen 1:1 In the beginning God",
          "created the heaven and the",
          "earth. And the earth was",
          "without form, and void...",
          "", "" }
    },
    {
        "Cls;",
        { "", "", "", "", "", "" }
    },
    {
        "MemPrint;",
        { "Heap: 137MB free / 512MB",
          "Code: 4MB",
          "Bible: loaded",
          "", "", "" }
    },
    {
        "God.Bible.Cnt;",
        { "0x00000000000034FF",
          "(13439 verses ready)",
          "", "", "", "" }
    },
    {
        "Halt;",
        { "System halted. Amen.",
          "", "", "", "", "" }
    },
};
#define N_COMMANDS (int)(sizeof(COMMANDS)/sizeof(COMMANDS[0]))

// ---- Terminal buffer (scrolling text lines) ----
#define TERM_ROWS 20
#define TERM_COLS 40
static char s_term[TERM_ROWS][TERM_COLS + 1];
static int  s_term_next;   // next line to write

static void term_reset(void)
{
    for (int i = 0; i < TERM_ROWS; i++) s_term[i][0] = 0;
    s_term_next = 0;
}

static void term_println(const char *s)
{
    if (s_term_next < TERM_ROWS) {
        strncpy(s_term[s_term_next], s, TERM_COLS);
        s_term[s_term_next][TERM_COLS] = 0;
        s_term_next++;
    } else {
        // Scroll up.
        for (int i = 1; i < TERM_ROWS; i++)
            memcpy(s_term[i - 1], s_term[i], TERM_COLS + 1);
        strncpy(s_term[TERM_ROWS - 1], s, TERM_COLS);
        s_term[TERM_ROWS - 1][TERM_COLS] = 0;
    }
}

// ---- Render ----

static void draw_scanlines(void)
{
    // Faint horizontal bands every 3rd row — light-gray specks on the
    // white bg so text stays readable but the panel reads as CRT.
    uint16_t band = rgb(C_LTGRAY);
    for (int y = 0; y < SCREEN_H; y += 3) {
        uint16_t *p = &s_fb[y * SCREEN_W];
        for (int x = 0; x < SCREEN_W; x += 4) p[x] = band;
    }
}

static void draw_static_flash(uint32_t now)
{
    // Every ~4.7 seconds paint a single-frame all-white blip so the
    // terminal has that "monitor blinked" quirk Terry's videos had.
    // Two frames of duration so it registers at 30fps.
    uint32_t phase = now % 4700;
    if (phase < 66) {
        fb_clear(C_WHITE);
    }
}

static void render(int sel, uint32_t now)
{
    // White background (was YELLOW in v1). Blue text stays — Terry's
    // Fs->text_attr = WHITE<<4 + BLUE reads the same way visually.
    fb_clear(C_WHITE);
    draw_scanlines();

    // Top border strip.
    fb_fill_rect(0, 0, SCREEN_W, GLYPH_H, C_BLUE);
    fb_puts_px((SCREEN_W - 22 * GLYPH_W) / 2, 0,
               "HOLYC SHELL v1.0 -- FS/1", C_WHITE, C_BLUE);

    // Terminal scroll area (rows 2..21).
    for (int i = 0; i < TERM_ROWS && i < 20; i++) {
        if (s_term[i][0])
            fb_puts(0, 2 + i, s_term[i], C_BLUE, C_WHITE);
    }

    // Prompt line with blinking underscore cursor.
    int prompt_row = 22;
    fb_puts(0, prompt_row, ">", C_LTRED, C_WHITE);
    fb_puts(2, prompt_row, COMMANDS[sel].cmd, C_BLUE, C_WHITE);
    if ((now / 400) & 1) {
        int cmd_len = (int)strlen(COMMANDS[sel].cmd);
        fb_puts(2 + cmd_len, prompt_row, "_", C_BLUE, C_WHITE);
    }

    // Bottom hint row.
    fb_fill_rect(0, (TEXT_ROWS - 1) * GLYPH_H, SCREEN_W, GLYPH_H, C_BLUE);
    fb_puts_px(2, (TEXT_ROWS - 1) * GLYPH_H,
               "U/D SELECT  A RUN  B CLS  BOOT EXIT",
               C_WHITE, C_BLUE);

    // Selection hint (arrow at the prompt line).
    fb_puts_px(SCREEN_W - 12 * GLYPH_W, prompt_row * GLYPH_H,
               "PICKED", C_LTRED, C_WHITE);
    char sel_buf[16];
    snprintf(sel_buf, sizeof(sel_buf), "%d/%d", sel + 1, N_COMMANDS);
    fb_puts_px(SCREEN_W - 5 * GLYPH_W, prompt_row * GLYPH_H,
               sel_buf, C_BLUE, C_WHITE);

    draw_static_flash(now);
    display_present_full(s_fb);
}

// ---- Main loop ----

static void run_command(int sel)
{
    // Echo the command as if the user typed and pressed Enter.
    char echo[64];
    snprintf(echo, sizeof(echo), "> %s", COMMANDS[sel].cmd);
    term_println(echo);

    // Cls; special-case: wipe the buffer.
    if (strcmp(COMMANDS[sel].cmd, "Cls;") == 0) {
        term_reset();
        return;
    }
    // Halt; special-case: beep and stop the world for a beat.
    if (strcmp(COMMANDS[sel].cmd, "Halt;") == 0) {
        term_println("System halted. Amen.");
        shrine_beep(200, 300);
        return;
    }

    for (int i = 0; i < 6; i++) {
        const char *ln = COMMANDS[sel].out[i];
        if (!ln || !*ln) break;
        if (strcmp(ln, OUT_GODWORD) == 0) {
            // Substitute a live GodWord — this is what makes "God;" feel
            // like Terry's actual RNG oracle: no two invocations agree.
            char buf[48];
            const char *w = VOCAB[shrine_god(VOCAB_N)];
            snprintf(buf, sizeof(buf), "  %s", w);
            term_println(buf);
        } else {
            term_println(ln);
        }
    }
    shrine_beep(1200, 30);
}

void game_holyc_run(void)
{
    term_reset();
    term_println("HolyC on Ring 0. Type; is 'A'.");
    term_println("");

    int sel = 0;
    uint32_t last_repaint = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        bool need_paint = false;
        if (shrine_key_pressed(BTN_UP))   { sel = (sel - 1 + N_COMMANDS) % N_COMMANDS; need_paint = true; shrine_beep(1600, 12); }
        if (shrine_key_pressed(BTN_DOWN)) { sel = (sel + 1) % N_COMMANDS;              need_paint = true; shrine_beep(1600, 12); }
        if (shrine_key_pressed(BTN_A))    { run_command(sel);                          need_paint = true; }
        if (shrine_key_pressed(BTN_B))    { term_reset(); need_paint = true; shrine_beep(800, 30); }

        uint32_t now = shrine_ms();
        // Repaint every ~80 ms so cursor blinks and static flash fires
        // even without input.
        if (need_paint || (now - last_repaint) > 80) {
            render(sel, now);
            last_repaint = now;
        }

        shrine_sleep_ms(30);
    }
}
