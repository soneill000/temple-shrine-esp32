// game_lora.c — "HOLYMESH" LoRa broadcast + receive scene.
// TempleOS-themed messages over 915 MHz to nearby Meshtastic nodes.
//
// PASS 1 (this file): scene UI + content library + LoRa driver hookup
// on the raw-bytes level. Broadcasts are wrapped in a minimal envelope
// (magic + type + text) — real Meshtastic packet framing lands in
// pass 2 (see meshtastic_frame.c, TODO). That means for the moment
// other TempleShrine badges will receive our messages but the wider
// Meshtastic network will see them as unrecognized packets on the
// LongFast channel.
//
// UX:
//   PRAISE HIM (A)    — pick a random Terry-themed message, show it
//                       one screen, press A again to broadcast.
//   BROWSE (LR)       — scroll the quote/word library, current entry
//                       shown in the top pane; A broadcasts it.
//   INBOX (UD toggle) — swap to the received-messages log.
//   B                 — cycle sub-modes (broadcast / inbox).
//   BOOT              — exit.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"
#include "vocab.h"
#include "terry_quotes.h"
#include "lora_radio.h"

#include "scene_fb.h"
#define s_fb g_scene_fb

#include <stdio.h>
#include <string.h>

// ---- Framebuffer helpers (single blit per frame) ----
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

// Word-wrap `text` into rows starting at (row_top, col_left); each row
// is `cols_w` chars wide; up to `max_rows` rows drawn.
static void fb_wrap_puts(int row_top, int col_left, int cols_w,
                         int max_rows, const char *text,
                         color_t fg, color_t bg)
{
    char buf[80];
    int row = row_top;
    while (*text && (row - row_top) < max_rows) {
        while (*text == ' ') text++;
        if (!*text) break;
        int n = 0, brk = 0;
        while (text[n] && n < cols_w) {
            if (text[n] == ' ') brk = n;
            n++;
        }
        if (text[n] && brk > 0) n = brk;
        int len = n < 79 ? n : 79;
        memcpy(buf, text, len);
        buf[len] = 0;
        fb_puts(col_left, row++, buf, fg, bg);
        text += n;
    }
}

// ---- Content ----
// The message pool: Terry quotes (from terry_quotes.h) + GodWords (VOCAB).
// Each entry is a pointer into read-only storage — we pick one and pass
// its bytes to lora_radio_send.

typedef enum { MSG_QUOTE, MSG_WORD } msg_kind_t;

typedef struct {
    msg_kind_t kind;
    int        index;      // into TERRY_QUOTES[] or VOCAB[]
} msg_ref_t;

static int msg_pool_size(void) { return TERRY_QUOTES_N + VOCAB_N; }

static msg_ref_t msg_at(int i)
{
    msg_ref_t r;
    if (i < TERRY_QUOTES_N) {
        r.kind = MSG_QUOTE;
        r.index = i;
    } else {
        r.kind = MSG_WORD;
        r.index = i - TERRY_QUOTES_N;
    }
    return r;
}

static void format_wire(const msg_ref_t *r, char *out, size_t max)
{
    // Wire format v1 (pre-Meshtastic wrap): "T1|<type>|<text>"
    //   'T1'  magic ("TempleShrine v1")
    //   type  'Q' quote  'W' word
    //   text  the payload text
    if (r->kind == MSG_QUOTE) {
        snprintf(out, max, "T1|Q|%s", TERRY_QUOTES[r->index].text);
    } else {
        snprintf(out, max, "T1|W|GOD SAYS: %s", VOCAB[r->index]);
    }
}

// ---- Inbox ring buffer ----
#define INBOX_MAX 16
static char     s_inbox[INBOX_MAX][160];
static int      s_inbox_head = 0;   // index of oldest
static int      s_inbox_n    = 0;

static void inbox_push(const char *text)
{
    int slot = (s_inbox_head + s_inbox_n) % INBOX_MAX;
    strncpy(s_inbox[slot], text, sizeof(s_inbox[0]) - 1);
    s_inbox[slot][sizeof(s_inbox[0]) - 1] = 0;
    if (s_inbox_n < INBOX_MAX) s_inbox_n++;
    else s_inbox_head = (s_inbox_head + 1) % INBOX_MAX;
}

// ---- UI modes ----
typedef enum { UI_PICK, UI_PREVIEW, UI_INBOX } ui_mode_t;

static void draw_frame(const char *title)
{
    fb_clear(C_BG);
    // Yellow top/bottom bands + side columns.
    fb_fill_rect(0, 0, SCREEN_W, GLYPH_H, C_YELLOW);
    fb_fill_rect(0, (TEXT_ROWS - 1) * GLYPH_H,
                 SCREEN_W, GLYPH_H, C_YELLOW);
    fb_fill_rect(0, GLYPH_H, GLYPH_W, SCREEN_H - 2 * GLYPH_H, C_YELLOW);
    fb_fill_rect(SCREEN_W - GLYPH_W, GLYPH_H, GLYPH_W,
                 SCREEN_H - 2 * GLYPH_H, C_YELLOW);
    fb_puts_centered(0, title, C_BG, C_YELLOW);
}

static void render_pick(int cur, bool radio_ok, int msg_count)
{
    draw_frame(" HOLYMESH  -  BROADCAST ");

    msg_ref_t r = msg_at(cur);
    const char *body;
    const char *cite = "";
    color_t     hue  = C_WHITE;
    if (r.kind == MSG_QUOTE) {
        body = TERRY_QUOTES[r.index].text;
        cite = TERRY_QUOTES[r.index].cite;
        hue  = C_LTGREEN;
        fb_puts(2, 2, "TERRY APHORISM", C_LTCYAN, C_BG);
    } else {
        body = VOCAB[r.index];
        hue  = C_YELLOW;
        fb_puts(2, 2, "GOD SAYS", C_LTCYAN, C_BG);
    }

    fb_wrap_puts(4, 2, 36, 12, body, hue, C_BG);

    if (cite && *cite) {
        char citebuf[64];
        snprintf(citebuf, sizeof(citebuf), "-- %s", cite);
        fb_puts(2, 18, citebuf, C_DKGRAY, C_BG);
    }

    // Index badge + radio status.
    char pg[32];
    snprintf(pg, sizeof(pg), "%d / %d", cur + 1, msg_count);
    fb_puts(TEXT_COLS - 12, 2, pg, C_LTCYAN, C_BG);
    // Radio status line — show the driver's own reason string when
    // offline so the user can see (SPI attach failed / no chip / wrong
    // ID) instead of a blank "OFFLINE".
    char statusbuf[48];
    if (radio_ok) {
        snprintf(statusbuf, sizeof(statusbuf), "RADIO: READY");
    } else {
        snprintf(statusbuf, sizeof(statusbuf), "RADIO OFF (%s)",
                 lora_radio_status());
    }
    fb_puts(2, 20, statusbuf, radio_ok ? C_LTGREEN : C_LTRED, C_BG);
    fb_puts(2, 21, "915 MHz  SF11  BW250  LongFast", C_DKGRAY, C_BG);

    // Received-count preview.
    char rxb[24];
    snprintf(rxb, sizeof(rxb), "INBOX %d", s_inbox_n);
    fb_puts(TEXT_COLS - 10, 20, rxb, C_LTMAGENTA, C_BG);

    fb_puts_centered(TEXT_ROWS - 1,
                     " LR BROWSE  A SEND  B INBOX ", C_BG, C_YELLOW);
    display_present_full(s_fb);
}

static void render_preview(int cur, const char *wire, bool sent, bool ok)
{
    draw_frame(sent ? " HOLYMESH  -  SENT " :
                      " HOLYMESH  -  BROADCASTING ");
    msg_ref_t r = msg_at(cur);
    const char *body = (r.kind == MSG_QUOTE)
                       ? TERRY_QUOTES[r.index].text
                       : VOCAB[r.index];

    fb_puts_centered(2, "PRAISE HIM", C_YELLOW, C_BG);
    fb_wrap_puts(5, 2, 36, 10, body, C_WHITE, C_BG);

    fb_puts(2, 16, "OUTGOING WIRE:", C_LTCYAN, C_BG);
    fb_wrap_puts(17, 2, 36, 3, wire, C_LTGREEN, C_BG);

    if (sent) {
        fb_puts(2, 22,
                ok ? "[SIGNAL RELEASED]" : "[TX FAILED - radio not ready]",
                ok ? C_LTGREEN : C_LTRED, C_BG);
    } else {
        fb_puts(2, 22, "SENDING...", C_YELLOW, C_BG);
    }

    fb_puts_centered(TEXT_ROWS - 1,
                     sent ? " A BACK  B BROWSE " : " ...transmitting ",
                     C_BG, C_YELLOW);
    display_present_full(s_fb);
}

static void render_inbox(int scroll_top)
{
    draw_frame(" HOLYMESH  -  INBOX ");
    if (s_inbox_n == 0) {
        fb_puts_centered(10, "no signals received yet.",
                         C_LTGRAY, C_BG);
        fb_puts_centered(12, "b returns to broadcast.",
                         C_DKGRAY, C_BG);
    } else {
        int rows = TEXT_ROWS - 4;
        for (int i = 0; i < rows && (i + scroll_top) < s_inbox_n; i++) {
            int slot = (s_inbox_head + i + scroll_top) % INBOX_MAX;
            fb_puts(2, 2 + i, s_inbox[slot], C_WHITE, C_BG);
        }
    }
    fb_puts_centered(TEXT_ROWS - 1,
                     " UD SCROLL  B BROADCAST  A EXIT ",
                     C_BG, C_YELLOW);
    display_present_full(s_fb);
}

// ---- Main loop ----

void game_lora_run(void)
{
    // Best-effort radio init (safe to fail; UI still works).
    bool radio_ok = lora_radio_init();

    int      cur      = 0;
    int      msg_n    = msg_pool_size();
    ui_mode_t mode    = UI_PICK;
    int      inbox_scroll = 0;
    char     wire[192];

    render_pick(cur, radio_ok, msg_n);

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        // Passive receive — poll every frame.
        {
            uint8_t buf[160];
            int rssi = 0;
            size_t n = lora_radio_recv(buf, sizeof(buf) - 1, &rssi);
            if (n > 0) {
                buf[n] = 0;
                // If prefixed with our magic, strip it for display.
                char line[192];
                const char *payload = (const char *)buf;
                if (n > 5 && buf[0] == 'T' && buf[1] == '1'
                    && buf[2] == '|') {
                    payload = (const char *)(buf + 5);
                }
                // Cap payload printout at 150 chars so the RSSI prefix
                // plus text always fits in `line`.
                snprintf(line, sizeof(line), "[R%d] %.150s", rssi, payload);
                inbox_push(line);
                shrine_beep(1800, 40);
            }
        }

        bool need_repaint = false;

        switch (mode) {
        case UI_PICK:
            if (shrine_key_pressed(BTN_LEFT)) {
                cur = (cur - 1 + msg_n) % msg_n;
                shrine_beep(1200, 15);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_RIGHT)) {
                cur = (cur + 1) % msg_n;
                shrine_beep(1200, 15);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_A)) {
                msg_ref_t r = msg_at(cur);
                format_wire(&r, wire, sizeof(wire));
                render_preview(cur, wire, false, false);
                shrine_beep(2200, 60);
                bool ok = lora_radio_send((const uint8_t *)wire,
                                          strlen(wire));
                render_preview(cur, wire, true, ok);
                mode = UI_PREVIEW;
                continue;
            }
            if (shrine_key_pressed(BTN_B)) {
                mode = UI_INBOX;
                inbox_scroll = 0;
                shrine_beep(1400, 20);
                render_inbox(inbox_scroll);
                continue;
            }
            if (need_repaint) render_pick(cur, radio_ok, msg_n);
            break;

        case UI_PREVIEW:
            if (shrine_key_pressed(BTN_A)) {
                mode = UI_PICK;
                render_pick(cur, radio_ok, msg_n);
            }
            if (shrine_key_pressed(BTN_B)) {
                mode = UI_PICK;
                render_pick(cur, radio_ok, msg_n);
            }
            break;

        case UI_INBOX:
            if (shrine_key_pressed(BTN_UP) && inbox_scroll > 0) {
                inbox_scroll--;
                render_inbox(inbox_scroll);
            }
            if (shrine_key_pressed(BTN_DOWN)
                && inbox_scroll < s_inbox_n - 1) {
                inbox_scroll++;
                render_inbox(inbox_scroll);
            }
            if (shrine_key_pressed(BTN_A)) return;
            if (shrine_key_pressed(BTN_B)) {
                mode = UI_PICK;
                render_pick(cur, radio_ok, msg_n);
            }
            break;
        }

        shrine_sleep_ms(30);
    }
}
