// game_bible.c — a small KJV reader in Terry's DolDoc blue/white style.
//
// Not a Terry-authored game — the user asked for a little Bible-verse
// browser matching TempleOS aesthetic. Content comes from bible.c's
// KJV_TABLE (real KJV passages where we have them, single-line
// paraphrases for the rest). Selection order matches KJV_TABLE order:
// Genesis -> Revelation.
//
// UI: TempleOS blue background, white text, yellow book/chapter header,
// LTCYAN citation subtitle. UP/DOWN scrolls within the current passage
// (long passages wrap over multiple lines). LEFT/RIGHT pages between
// entries. A/BOOT exits.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "bible.h"
#include "display.h"

#include "scene_fb.h"
#define s_fb g_scene_fb

#include <stdio.h>
#include <string.h>

// --- Framebuffer helpers (write into s_fb, single blit per frame) ---
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
        for (int col = 0; col < 8; col++) {
            fb_pixel(x + col, y + row, (bits & (1 << col)) ? fg : bg);
        }
    }
}
static void fb_puts(int col, int row, const char *s, color_t fg, color_t bg)
{
    int x = col * GLYPH_W, y = row * GLYPH_H;
    while (*s) { fb_putc(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static void fb_puts_centered(int row, const char *s, color_t fg, color_t bg)
{
    int n = 0; while (s[n]) n++;
    int x = (SCREEN_W - n * GLYPH_W) / 2;
    int y = row * GLYPH_H;
    while (*s) { fb_putc(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static inline void fb_clear(color_t c)
{
    uint16_t v = rgb(c);
    int n = SCREEN_W * SCREEN_H;
    for (int i = 0; i < n; i++) s_fb[i] = v;
}

// --- Word wrap the KJV text into lines of `cols` chars ---
#define BIBLE_MAX_LINES 40
#define BIBLE_LINE_COLS 38     // 38 * 8 = 304 fb px, fits in 320 with 8 px margins

typedef struct {
    const char *start;
    uint8_t     len;
} bible_line_t;

static int wrap_lines(const char *s, int cols,
                      bible_line_t *out, int max_lines)
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

// Draw Terry-style DolDoc window frame: yellow border rows top+bottom
// on a blue interior. Matches Terry's PopUp() presentation for text
// browsers like his ::/Adam/God/godbible.cpp BibleVerse output.
static void draw_frame(const char *title)
{
    fb_clear(C_BG);
    // Yellow top and bottom border rows (full-width bands).
    fb_fill_rect(0, 0, SCREEN_W, GLYPH_H, PAL_RGB565[C_YELLOW]);
    fb_fill_rect(0, (TEXT_ROWS - 1) * GLYPH_H,
                 SCREEN_W, GLYPH_H, PAL_RGB565[C_YELLOW]);
    // Vertical yellow border columns.
    fb_fill_rect(0,             GLYPH_H, GLYPH_W, SCREEN_H - 2 * GLYPH_H,
                 PAL_RGB565[C_YELLOW]);
    fb_fill_rect(SCREEN_W - GLYPH_W, GLYPH_H, GLYPH_W, SCREEN_H - 2 * GLYPH_H,
                 PAL_RGB565[C_YELLOW]);
    // Title text in the top yellow band (blue text on yellow).
    fb_puts_centered(0, title, C_BG, C_YELLOW);
}

static void render_passage(int idx, int scroll_top)
{
    const kjv_verse_t *v = &KJV_TABLE[idx];

    draw_frame(" B I B L E   -   K J V ");

    // Book/chapter/verse citation row (row 2), yellow-highlight strip.
    char cite[48];
    snprintf(cite, sizeof(cite), " %s %d:%d ",
             v->book, v->chapter, v->verse);
    fb_fill_rect(GLYPH_W * 2, 2 * GLYPH_H,
                 SCREEN_W - 4 * GLYPH_W, GLYPH_H,
                 PAL_RGB565[C_LTCYAN]);
    fb_puts_centered(2, cite, C_BG, C_LTCYAN);

    // Yellow separator under citation.
    fb_fill_rect(GLYPH_W * 2, 3 * GLYPH_H + 3,
                 SCREEN_W - 4 * GLYPH_W, 1,
                 PAL_RGB565[C_YELLOW]);

    // Body: wrapped white text in the blue interior.
    bible_line_t lines[BIBLE_MAX_LINES];
    int total = wrap_lines(v->text, BIBLE_LINE_COLS, lines, BIBLE_MAX_LINES);

    const int body_top    = 5;
    const int body_rows   = 22;
    const int body_left_x = GLYPH_W * 2;   // inside the vertical border

    if (scroll_top < 0) scroll_top = 0;
    int max_scroll = total > body_rows ? total - body_rows : 0;
    if (scroll_top > max_scroll) scroll_top = max_scroll;

    for (int i = 0; i < body_rows && (i + scroll_top) < total; i++) {
        int li = i + scroll_top;
        int len = lines[li].len;
        if (len > BIBLE_LINE_COLS) len = BIBLE_LINE_COLS;
        char buf[BIBLE_LINE_COLS + 1];
        memcpy(buf, lines[li].start, len);
        buf[len] = 0;
        int x = body_left_x;
        int y = (body_top + i) * GLYPH_H;
        const char *p = buf;
        while (*p) {
            fb_putc(x, y, *p++, C_WHITE, C_BG);
            x += GLYPH_W;
        }
    }

    // Scroll indicators.
    if (scroll_top > 0)
        fb_puts(TEXT_COLS - 3, body_top, "^", C_LTGREEN, C_BG);
    if (scroll_top < max_scroll)
        fb_puts(TEXT_COLS - 3, body_top + body_rows - 1, "v", C_LTGREEN, C_BG);

    // Footer hint in the bottom yellow band.
    fb_puts_centered(TEXT_ROWS - 1,
                     " LR PASSAGE  UD SCROLL  B INDEX  A EXIT ",
                     C_BG, C_YELLOW);

    // Passage-index badge in the top-right corner of the citation row.
    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", idx + 1, KJV_TABLE_N);
    int pg_len = (int)strlen(pg);
    fb_puts(TEXT_COLS - 2 - pg_len, 2, pg, C_BG, C_LTCYAN);

    display_present_full(s_fb);
}

// Book-index screen — lists every unique book name in KJV_TABLE and
// lets the user jump straight to that book's first verse. Terry's
// godbible.cpp defines all 66 canonical books; we only expose the
// ones we actually have verses for.
static void render_index(int sel, int scroll_top,
                         const int *first_idx, const char *const *names,
                         int books_n)
{
    draw_frame(" B I B L E   -   B O O K S ");
    // Column layout inside the yellow border, 2 cols of 12 rows each.
    const int body_top  = 3;
    const int per_col   = 12;
    const int cols      = 2;
    const int col_w     = (TEXT_COLS - 4) / cols;    // usable width inside borders
    int visible = per_col * cols;
    if (scroll_top > books_n - visible) scroll_top = books_n - visible;
    if (scroll_top < 0) scroll_top = 0;

    for (int i = 0; i < visible; i++) {
        int bi = i + scroll_top;
        if (bi >= books_n) break;
        int col = i / per_col;
        int row = body_top + (i % per_col);
        int text_col = 2 + col * col_w;
        if (bi == sel) {
            // Yellow highlight strip for selected book.
            fb_fill_rect(text_col * GLYPH_W, row * GLYPH_H,
                         (col_w - 1) * GLYPH_W, GLYPH_H,
                         PAL_RGB565[C_YELLOW]);
            fb_puts(text_col, row, names[bi], C_BG, C_YELLOW);
        } else {
            fb_puts(text_col, row, names[bi], C_WHITE, C_BG);
        }
    }

    fb_puts_centered(TEXT_ROWS - 1,
                     " UD SELECT  A OPEN  B BACK ",
                     C_BG, C_YELLOW);

    display_present_full(s_fb);
}

// Build the list of unique book names and the first KJV_TABLE index
// for each. Books appear in their original bible.c order.
static int build_book_index(int *first_idx, const char **names, int max_books)
{
    int n = 0;
    for (int i = 0; i < KJV_TABLE_N && n < max_books; i++) {
        const char *b = KJV_TABLE[i].book;
        bool seen = false;
        for (int j = 0; j < n; j++) {
            if (strcmp(names[j], b) == 0) { seen = true; break; }
        }
        if (!seen) {
            names[n]     = b;
            first_idx[n] = i;
            n++;
        }
    }
    return n;
}

void game_bible_run(void)
{
    // Build the book index (unique books in KJV_TABLE order).
    #define MAX_BOOKS 24
    const char *book_names[MAX_BOOKS];
    int         book_first[MAX_BOOKS];
    int         books_n = build_book_index(book_first, book_names, MAX_BOOKS);

    // Two modes: showing the book index, or reading a passage.
    enum { MODE_INDEX, MODE_READ } mode = MODE_INDEX;
    int book_sel        = 0;
    int book_scroll_top = 0;
    int idx             = 0;
    int scroll_top      = 0;

    render_index(book_sel, book_scroll_top, book_first, book_names, books_n);

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        bool need_repaint = false;

        if (mode == MODE_INDEX) {
            if (shrine_key_pressed(BTN_A)) {
                // Open the selected book at its first verse. Render
                // the passage view directly and continue -- without
                // this, we'd fall through to the render_index() call
                // below and paint the index over the passage until the
                // user pressed another key that triggered a repaint,
                // which read as "A did nothing until I pressed down."
                idx        = book_first[book_sel];
                scroll_top = 0;
                mode       = MODE_READ;
                shrine_beep(1800, 40);
                render_passage(idx, scroll_top);
                continue;
            }
            if (shrine_key_pressed(BTN_B)) {
                // B from index exits back to the launcher menu.
                return;
            }
            if (shrine_key_pressed(BTN_UP)) {
                book_sel = (book_sel - 1 + books_n) % books_n;
                if (book_sel < book_scroll_top) book_scroll_top = book_sel;
                shrine_beep(1200, 20);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_DOWN)) {
                book_sel = (book_sel + 1) % books_n;
                if (book_sel >= book_scroll_top + 24) book_scroll_top++;
                shrine_beep(1200, 20);
                need_repaint = true;
            }
            if (need_repaint)
                render_index(book_sel, book_scroll_top,
                             book_first, book_names, books_n);
        } else {  // MODE_READ
            if (shrine_key_pressed(BTN_A)) return;   // exit to launcher
            if (shrine_key_pressed(BTN_B)) {
                // Back to book index.
                mode = MODE_INDEX;
                shrine_beep(1400, 30);
                render_index(book_sel, book_scroll_top,
                             book_first, book_names, books_n);
                continue;
            }
            if (shrine_key_pressed(BTN_LEFT)) {
                idx = (idx - 1 + KJV_TABLE_N) % KJV_TABLE_N;
                scroll_top = 0;
                shrine_beep(1200, 20);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_RIGHT)) {
                idx = (idx + 1) % KJV_TABLE_N;
                scroll_top = 0;
                shrine_beep(1200, 20);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_UP) && scroll_top > 0) {
                scroll_top--;
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_DOWN)) {
                scroll_top++;
                need_repaint = true;
            }
            if (need_repaint) render_passage(idx, scroll_top);
        }

        shrine_sleep_ms(30);
    }
}
