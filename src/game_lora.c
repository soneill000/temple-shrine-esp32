// game_lora.c — "HOLYMESH" LoRa broadcast + receive scene.
//
// Broadcasts TempleOS-themed messages on the Meshtastic US LongFast
// primary channel (906.875 MHz, SF11 BW250, sync 0x2B). Every outgoing
// message is packed into a real Meshtastic v2 mesh packet via
// meshtastic_frame.c — 16-byte header + AES-128-CTR-encrypted Data
// protobuf (portnum=TEXT_MESSAGE_APP). Meshtastic phones/devices on
// the same channel will pick these up and render them as text
// messages. Incoming frames are decrypted and displayed in the inbox
// if they parse as text on the same channel.
//
// UX:
//   COMPOSE (default) — random 4-8 GodWord sequence; A rerolls, DN
//                       clears, B broadcasts, UP -> BROWSE.
//   BROWSE            — curated Terry aphorisms; LEFT/RIGHT scrolls,
//                       A broadcasts, B -> INBOX, UP -> COMPOSE.
//   INBOX             — received messages (with sender node IDs).
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
#include "meshtastic_frame.h"

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
// Browse/pick mode only lists Terry quotes. GodWord broadcasting lives
// exclusively in COMPOSE mode (random sequences) so this section stays
// curated. All broadcasts go out as real Meshtastic text messages via
// meshtastic_build_text — the text body is the phrase itself, exactly
// as any Meshtastic device would send.

static int msg_pool_size(void) { return TERRY_QUOTES_N; }

// Broadcast a text string as a Meshtastic v2 text message on LongFast.
// Returns whether the LoRa TX completed. Fills wire_hex_out (optional)
// with a short preview like "TX 84B #a1b2c3d4" for on-screen display.
static bool send_meshtastic_text(const char *text, char *tx_status_out, size_t tsz)
{
    uint8_t frame[MESHTASTIC_MAX_FRAME];
    uint32_t packet_id = shrine_god(0x7fffffff);
    if (packet_id == 0) packet_id = 1;
    size_t n = meshtastic_build_text(text, packet_id, frame, sizeof(frame));
    if (n == 0) {
        if (tx_status_out) snprintf(tx_status_out, tsz, "encode failed");
        return false;
    }
    bool ok = lora_radio_send(frame, n);
    if (tx_status_out)
        snprintf(tx_status_out, tsz, "%s %uB #%08lx",
                 ok ? "TX" : "FAIL", (unsigned)n, (unsigned long)packet_id);
    return ok;
}

// Broadcast a NODEINFO_APP frame so other Meshtastic devices get a
// User record for the badge and stop hiding text messages from us as
// "unknown sender." Called once on scene entry and re-triggerable
// via a UI key.
static bool send_meshtastic_nodeinfo(void)
{
    uint8_t frame[MESHTASTIC_MAX_FRAME];
    uint32_t packet_id = shrine_god(0x7fffffff);
    if (packet_id == 0) packet_id = 1;
    size_t n = meshtastic_build_nodeinfo("TempleShrine", "TMPL",
                                         packet_id,
                                         frame, sizeof(frame));
    if (n == 0) return false;
    return lora_radio_send(frame, n);
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

// ---- RX message flash + fanfare ----
// A small on-screen popup + horn fanfare when a new text arrives so
// the user notices even if they're in COMPOSE/BROWSE/SCAN rather than
// staring at INBOX.
#define RX_FLASH_MS 1800u
static uint32_t s_rx_flash_until;
static uint32_t s_rx_flash_from;
static char     s_rx_flash_preview[64];

// The badge piezo is a MOSFET-driven part with resonance around 4 kHz
// (per hw.h). Anything below ~1.5 kHz barely rings out; the sweet
// spot is 2..4 kHz. Both fanfares live in that band so they're
// actually audible instead of a muffled buzz.

// RX fanfare — four-note ascending C major arpeggio in the piezo's
// hot band. C7 E7 G7 C8. Reads as a "you got mail!" trumpet call.
static void play_message_fanfare(void)
{
    shrine_beep(2093,  90);   // C7
    shrine_beep(2637,  90);   // E7
    shrine_beep(3136, 120);   // G7
    shrine_beep(4186, 220);   // C8 (held, right at piezo resonance)
}

// TX fanfare — short-short-long DESCENDING pattern so it's audibly
// distinct from the RX call. G7 E7 C7. Reads as "message outbound."
static void play_send_fanfare(void)
{
    shrine_beep(3136,  80);   // G7
    shrine_beep(2637,  80);   // E7
    shrine_beep(2093, 200);   // C7 (held)
}

static void trigger_rx_flash(uint32_t from, const char *text)
{
    s_rx_flash_until = shrine_ms() + RX_FLASH_MS;
    s_rx_flash_from  = from;
    // Preview = first ~40 chars of the text so the popup is readable
    // even for long messages.
    strncpy(s_rx_flash_preview, text ? text : "",
            sizeof(s_rx_flash_preview) - 1);
    s_rx_flash_preview[sizeof(s_rx_flash_preview) - 1] = 0;
    play_message_fanfare();
}

// Draw the "message received" popup overlay on top of whatever mode is
// currently rendered. No-op if the flash window has expired. Called
// after each mode's own render so the popup floats above.
static void draw_rx_flash_if_active(void)
{
    if (!s_rx_flash_until) return;
    uint32_t now = shrine_ms();
    if (now >= s_rx_flash_until) { s_rx_flash_until = 0; return; }

    // Centered box, 6 rows tall x 34 cols wide.
    const int box_rows = 6;
    const int box_cols = 34;
    int px = (SCREEN_W - box_cols * GLYPH_W) / 2;
    int py = (SCREEN_H - box_rows * GLYPH_H) / 2;
    // Border color blinks between LTGREEN and YELLOW so it draws the
    // eye without being annoying.
    color_t border = ((now / 150) & 1) ? C_LTGREEN : C_YELLOW;
    // Solid background.
    fb_fill_rect(px - 4, py - 4,
                 box_cols * GLYPH_W + 8, box_rows * GLYPH_H + 8, border);
    fb_fill_rect(px - 2, py - 2,
                 box_cols * GLYPH_W + 4, box_rows * GLYPH_H + 4, C_BG);
    // Header
    fb_puts((px / GLYPH_W) + 1, (py / GLYPH_H),
            "*  MESSAGE RECEIVED  *", C_LTGREEN, C_BG);
    // From
    char fromb[24];
    snprintf(fromb, sizeof(fromb), "from %08lx", (unsigned long)s_rx_flash_from);
    fb_puts((px / GLYPH_W) + 1, (py / GLYPH_H) + 2,
            fromb, C_LTCYAN, C_BG);
    // Preview (single line, truncated).
    fb_puts((px / GLYPH_W) + 1, (py / GLYPH_H) + 4,
            s_rx_flash_preview, C_WHITE, C_BG);
    display_present_full(s_fb);
}

// ---- UI modes ----
// UI_COMPOSE: sequence GodWords into a phrase, send when ready (first
//             option in the scene; matches user request).
// UI_PICK:    browse curated Terry aphorisms + single GodWord entries,
//             send the highlighted one with A.
// UI_PREVIEW: post-send confirmation screen.
// UI_INBOX:   received messages log.
// UI_SCAN:    passive scanner — table of every Meshtastic node we've
//             heard on air, with RSSI and channel-match indicator.
typedef enum { UI_COMPOSE, UI_PICK, UI_PREVIEW, UI_INBOX, UI_SCAN } ui_mode_t;

// ---- Node scanner table ----
// Every time we hear ANY frame on air (regardless of whether we can
// decrypt it), we record the source node ID + RSSI + channel hash.
// Same node ID => update in place; LRU-evict when the table is full.
#define SCAN_NODES 16
typedef struct {
    uint32_t node_id;
    int8_t   rssi;
    uint8_t  channel_hash;
    bool     on_our_channel;
    bool     saw_text;       // did we successfully decrypt a text msg from them?
    uint32_t last_seen_ms;
    uint32_t heard_count;
} scan_node_t;
static scan_node_t s_nodes[SCAN_NODES];
static int         s_node_count;
static uint32_t    s_raw_packets;      // any RX at all (including undecodable garbage)
static uint32_t    s_header_ok;        // header parsed as a valid Meshtastic frame

static void scan_note_packet(uint32_t from, uint8_t channel_hash,
                             int rssi, bool text_ok)
{
    uint32_t now = shrine_ms();
    // Find existing.
    int slot = -1;
    for (int i = 0; i < s_node_count; i++) {
        if (s_nodes[i].node_id == from) { slot = i; break; }
    }
    if (slot < 0) {
        if (s_node_count < SCAN_NODES) {
            slot = s_node_count++;
        } else {
            // LRU-evict oldest last_seen.
            uint32_t oldest = 0xFFFFFFFFu;
            for (int i = 0; i < SCAN_NODES; i++) {
                if (s_nodes[i].last_seen_ms < oldest) {
                    oldest = s_nodes[i].last_seen_ms;
                    slot = i;
                }
            }
            memset(&s_nodes[slot], 0, sizeof(s_nodes[slot]));
        }
    }
    s_nodes[slot].node_id      = from;
    s_nodes[slot].rssi         = (int8_t)rssi;
    s_nodes[slot].channel_hash = channel_hash;
    s_nodes[slot].on_our_channel = (channel_hash == MESHTASTIC_LONGFAST_CHANNEL_HASH);
    if (text_ok) s_nodes[slot].saw_text = true;
    s_nodes[slot].last_seen_ms = now;
    s_nodes[slot].heard_count++;
}

// ---- Compose state ----
// Terry's own GodWord shuffles the RNG to sequence words divinely; we
// match that spirit by generating a random 4-8 word sequence prefixed
// with "GOD SAYS:". User can re-roll for a fresh sequence or clear to
// an empty phrase; no manual word picking (Terry doesn't let you pick
// either — the whole point is the machine speaks for God). Sent as
// "T1|C|<sequence>" so receivers can tell composed messages from
// curated ones.
#define COMPOSE_MAX 160
#define COMPOSE_PREFIX "GOD SAYS:"
static char s_compose[COMPOSE_MAX];

static void compose_clear(void)
{
    s_compose[0] = 0;
}

static void compose_reroll(void)
{
    // 4..8 words per sequence — enough for a saying, short enough to
    // stay readable on the panel.
    int n_words = 4 + (int)shrine_god(5);
    strncpy(s_compose, COMPOSE_PREFIX, COMPOSE_MAX - 1);
    s_compose[COMPOSE_MAX - 1] = 0;
    for (int i = 0; i < n_words; i++) {
        const char *w = VOCAB[shrine_god(VOCAB_N)];
        int cur_len = (int)strlen(s_compose);
        int add_len = (int)strlen(w) + 1;
        if (cur_len + add_len >= COMPOSE_MAX - 1) break;
        s_compose[cur_len] = ' ';
        strncpy(&s_compose[cur_len + 1], w, COMPOSE_MAX - cur_len - 2);
        s_compose[COMPOSE_MAX - 1] = 0;
    }
}

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

static void render_compose(bool radio_ok)
{
    draw_frame(" HOLYMESH  -  COMPOSE ");

    fb_puts(2, 2, "GOD SEQUENCED:", C_LTCYAN, C_BG);
    bool empty = (s_compose[0] == 0);
    if (empty) {
        fb_puts(2, 5, "(phrase cleared)", C_DKGRAY, C_BG);
        fb_puts(2, 7, "press A to reroll a new phrase", C_LTGRAY, C_BG);
    } else {
        // Show the current sequence wrapped to 36 cols across ~14 rows.
        fb_wrap_puts(4, 2, 36, 14, s_compose, C_LTGREEN, C_BG);
    }

    // Radio status.
    char statusbuf[48];
    if (radio_ok) snprintf(statusbuf, sizeof(statusbuf), "RADIO: READY");
    else          snprintf(statusbuf, sizeof(statusbuf), "RADIO OFF (%s)",
                           lora_radio_status());
    fb_puts(2, 20, statusbuf, radio_ok ? C_LTGREEN : C_LTRED, C_BG);
    char meta[48];
    snprintf(meta, sizeof(meta),
             "915MHz LongFast   ID %08lx",
             (unsigned long)meshtastic_my_node_id());
    fb_puts(2, 21, meta, C_DKGRAY, C_BG);

    // Received-count preview.
    char rxb[24];
    snprintf(rxb, sizeof(rxb), "INBOX %d", s_inbox_n);
    fb_puts(TEXT_COLS - 10, 20, rxb, C_LTMAGENTA, C_BG);

    fb_puts_centered(TEXT_ROWS - 1,
                     " A REROLL  B SEND  DN CLEAR   LR TAB ",
                     C_BG, C_YELLOW);
    display_present_full(s_fb);
}

static void render_pick(int cur, bool radio_ok, int msg_count)
{
    draw_frame(" HOLYMESH  -  BROADCAST ");

    const char *body = TERRY_QUOTES[cur].text;
    const char *cite = TERRY_QUOTES[cur].cite;
    fb_puts(2, 2, "TERRY APHORISM", C_LTCYAN, C_BG);
    fb_wrap_puts(4, 2, 36, 12, body, C_LTGREEN, C_BG);

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
    char meta2[48];
    snprintf(meta2, sizeof(meta2),
             "915MHz LongFast   ID %08lx",
             (unsigned long)meshtastic_my_node_id());
    fb_puts(2, 21, meta2, C_DKGRAY, C_BG);

    // Received-count preview.
    char rxb[24];
    snprintf(rxb, sizeof(rxb), "INBOX %d", s_inbox_n);
    fb_puts(TEXT_COLS - 10, 20, rxb, C_LTMAGENTA, C_BG);

    fb_puts_centered(TEXT_ROWS - 1,
                     " UD PICK  A SEND   LR TAB ",
                     C_BG, C_YELLOW);
    display_present_full(s_fb);
}

static void render_preview(const char *body, const char *wire, bool sent, bool ok)
{
    draw_frame(sent ? " HOLYMESH  -  SENT " :
                      " HOLYMESH  -  BROADCASTING ");

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
                     " UD SCROLL   LR TAB   BOOT EXIT ",
                     C_BG, C_YELLOW);
    display_present_full(s_fb);
}

static uint32_t s_render_scan_last_ni_ms;   // set by main loop before each render

static void render_scan(void)
{
    draw_frame(" HOLYMESH  -  SCAN ");
    fb_puts(2, 2, "MESHTASTIC NODES HEARD", C_LTCYAN, C_BG);

    char stats[48];
    snprintf(stats, sizeof(stats),
             "raw:%lu  hdr:%lu  known:%d/%d",
             (unsigned long)s_raw_packets,
             (unsigned long)s_header_ok,
             s_node_count, SCAN_NODES);
    fb_puts(2, 3, stats, C_DKGRAY, C_BG);

    // Show when our next NodeInfo re-announce is due so the user
    // knows the badge is periodically introducing itself.
    if (s_render_scan_last_ni_ms) {
        uint32_t now = shrine_ms();
        uint32_t since = (now - s_render_scan_last_ni_ms) / 1000;
        uint32_t until = 120 > since ? 120 - since : 0;
        char nib[40];
        snprintf(nib, sizeof(nib), "self-announce in %lus",
                 (unsigned long)until);
        fb_puts(TEXT_COLS - 24, 3, nib, C_DKGRAY, C_BG);
    }

    if (s_node_count == 0) {
        fb_puts_centered(11, "listening...", C_LTGRAY, C_BG);
        fb_puts_centered(13, "no packets on air yet",
                         C_DKGRAY, C_BG);
        fb_puts_centered(14,
                         "if this stays 0, RX is broken",
                         C_DKGRAY, C_BG);
    } else {
        // Column headers.
        fb_puts(2, 5, "ID       RSSI  CH  AGE   MSGS", C_LTCYAN, C_BG);
        uint32_t now = shrine_ms();
        // Show up to 12 rows; newest last-seen at top.
        // Simple selection sort by last_seen desc (small N).
        int idx[SCAN_NODES];
        for (int i = 0; i < s_node_count; i++) idx[i] = i;
        for (int i = 0; i < s_node_count; i++) {
            for (int j = i + 1; j < s_node_count; j++) {
                if (s_nodes[idx[j]].last_seen_ms >
                    s_nodes[idx[i]].last_seen_ms) {
                    int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
                }
            }
        }
        int show = s_node_count < 12 ? s_node_count : 12;
        for (int i = 0; i < show; i++) {
            const scan_node_t *n = &s_nodes[idx[i]];
            char row[48];
            uint32_t age_s = (now - n->last_seen_ms) / 1000;
            if (age_s > 999) age_s = 999;
            char chan_tag = n->on_our_channel ? '*' : ' ';
            snprintf(row, sizeof(row),
                     "%08lx %4d %02x%c %4lus %5lu",
                     (unsigned long)n->node_id,
                     (int)n->rssi,
                     n->channel_hash, chan_tag,
                     (unsigned long)age_s,
                     (unsigned long)n->heard_count);
            color_t hue = n->saw_text     ? C_LTGREEN
                        : n->on_our_channel ? C_YELLOW
                        : C_LTGRAY;
            fb_puts(2, 6 + i, row, hue, C_BG);
        }
    }

    fb_puts(2, 20, "* on our channel   green: text OK",
            C_DKGRAY, C_BG);
    char meta[48];
    snprintf(meta, sizeof(meta),
             "our ID %08lx  ch %02x",
             (unsigned long)meshtastic_my_node_id(),
             MESHTASTIC_LONGFAST_CHANNEL_HASH);
    fb_puts(2, 21, meta, C_DKGRAY, C_BG);

    fb_puts_centered(TEXT_ROWS - 1,
                     " A CLEAR  B ANNOUNCE   LR TAB   BOOT EXIT ",
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
    ui_mode_t mode    = UI_COMPOSE;    // land on COMPOSE first (user request)
    ui_mode_t preview_return = UI_COMPOSE;   // where to go back to after send
    int      inbox_scroll = 0;

    compose_reroll();
    render_compose(radio_ok);

    // Introduce ourselves to the mesh so text-message receivers have
    // a User record for the badge (Meshtastic apps hide text messages
    // from senders they don't know). Fires once on entry and then
    // every ~120 s while the scene is open — matches how Meshtastic
    // firmware itself periodically re-broadcasts NodeInfo.
    uint32_t last_nodeinfo_ms = 0;
    if (radio_ok && send_meshtastic_nodeinfo()) {
        last_nodeinfo_ms = shrine_ms();
    }

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        // Passive receive — poll every frame. Every raw packet gets
        // counted; well-formed Meshtastic headers feed the node
        // scanner table; text-message frames on our channel land in
        // the inbox with the decrypted text.
        {
            uint8_t buf[MESHTASTIC_MAX_FRAME];
            int rssi = 0;
            size_t n = lora_radio_recv(buf, sizeof(buf), &rssi);
            if (n > 0) {
                s_raw_packets++;
                uint32_t hdr_from = 0;
                uint8_t  hdr_chan = 0;
                bool header_ok = meshtastic_parse_header(buf, n, NULL,
                                                         &hdr_from, NULL,
                                                         &hdr_chan, NULL);
                if (header_ok) s_header_ok++;
                // Try full text decode for the inbox.
                char text[180];
                uint32_t from = 0;
                bool text_ok = meshtastic_parse_text(buf, n, text,
                                                    sizeof(text), &from);
                if (text_ok) {
                    char line[220];
                    snprintf(line, sizeof(line),
                             "[R%d %08lx] %.140s",
                             rssi, (unsigned long)from, text);
                    inbox_push(line);
                    // Skip the popup + fanfare when the "received"
                    // message is actually our own transmission looped
                    // back via a mesh rebroadcast — no need to alert
                    // the user about their own send. Still lands in
                    // inbox history for audit.
                    if (from != meshtastic_my_node_id()) {
                        trigger_rx_flash(from, text);
                    }
                }
                if (header_ok) {
                    scan_note_packet(hdr_from, hdr_chan, rssi, text_ok);
                }
            }
        }

        // Periodic NodeInfo re-announce (every 120 s). Matches how
        // Meshtastic firmware re-broadcasts NodeInfo so nodes that
        // come online later still learn about us.
        if (radio_ok) {
            uint32_t now_ms = shrine_ms();
            if (now_ms - last_nodeinfo_ms > 120000) {
                if (send_meshtastic_nodeinfo()) last_nodeinfo_ms = now_ms;
            }
        }
        s_render_scan_last_ni_ms = last_nodeinfo_ms;

        bool need_repaint = false;

        // Global tab cycle: LEFT/RIGHT walks the fixed sequence
        // COMPOSE -> PICK -> INBOX -> SCAN -> COMPOSE, skipping the
        // out-of-band PREVIEW mode. Consistent everywhere so the
        // player doesn't have to memorise which mode is reachable from
        // which other mode.
        static const ui_mode_t TAB_ORDER[4] = {
            UI_COMPOSE, UI_PICK, UI_INBOX, UI_SCAN
        };
        if (mode != UI_PREVIEW &&
            (shrine_key_pressed(BTN_LEFT) || shrine_key_pressed(BTN_RIGHT))) {
            int idx = 0;
            for (int i = 0; i < 4; i++) if (TAB_ORDER[i] == mode) { idx = i; break; }
            int delta = shrine_key_pressed(BTN_RIGHT) ? 1 : 3;
            ui_mode_t next = TAB_ORDER[(idx + delta) % 4];
            mode = next;
            shrine_beep(1400, 20);
            switch (next) {
                case UI_COMPOSE: render_compose(radio_ok); break;
                case UI_PICK:    render_pick(cur, radio_ok, msg_n); break;
                case UI_INBOX:   render_inbox(inbox_scroll); break;
                case UI_SCAN:    render_scan(); break;
                default: break;
            }
            continue;
        }

        switch (mode) {
        case UI_COMPOSE:
            // A reroll, B send, DOWN clear. UP unused.
            if (shrine_key_pressed(BTN_A)) {
                compose_reroll();
                shrine_beep(1800, 20);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_DOWN)) {
                compose_clear();
                shrine_beep(600, 30);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_B)) {
                // Send the composed phrase (skip if user cleared).
                if (s_compose[0] == 0) {
                    shrine_beep(300, 60);
                    break;
                }
                char tx_status[48];
                render_preview(s_compose, "sending...", false, false);
                play_send_fanfare();
                bool ok = send_meshtastic_text(s_compose,
                                               tx_status, sizeof(tx_status));
                render_preview(s_compose, tx_status, true, ok);
                mode = UI_PREVIEW;
                preview_return = UI_COMPOSE;
                continue;
            }
            if (need_repaint) render_compose(radio_ok);
            break;

        case UI_PICK:
            // UP/DOWN cycles quotes (used to be LEFT/RIGHT before the
            // tab-cycle rework). A sends. B unused.
            if (shrine_key_pressed(BTN_UP)) {
                cur = (cur - 1 + msg_n) % msg_n;
                shrine_beep(1200, 15);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_DOWN)) {
                cur = (cur + 1) % msg_n;
                shrine_beep(1200, 15);
                need_repaint = true;
            }
            if (shrine_key_pressed(BTN_A)) {
                const char *body = TERRY_QUOTES[cur].text;
                char tx_status[48];
                render_preview(body, "sending...", false, false);
                play_send_fanfare();
                bool ok = send_meshtastic_text(body,
                                               tx_status, sizeof(tx_status));
                render_preview(body, tx_status, true, ok);
                mode = UI_PREVIEW;
                preview_return = UI_PICK;
                continue;
            }
            if (need_repaint) render_pick(cur, radio_ok, msg_n);
            break;

        case UI_PREVIEW:
            if (shrine_key_pressed(BTN_A) || shrine_key_pressed(BTN_B)) {
                mode = preview_return;
                if (mode == UI_COMPOSE) render_compose(radio_ok);
                else                    render_pick(cur, radio_ok, msg_n);
            }
            break;

        case UI_INBOX:
            // UP/DOWN scroll. A/B unused (BOOT still exits scene).
            if (shrine_key_pressed(BTN_UP) && inbox_scroll > 0) {
                inbox_scroll--;
                render_inbox(inbox_scroll);
            }
            if (shrine_key_pressed(BTN_DOWN)
                && inbox_scroll < s_inbox_n - 1) {
                inbox_scroll++;
                render_inbox(inbox_scroll);
            }
            break;

        case UI_SCAN: {
            // A clears the table + counters. B re-announces our
            // NodeInfo (moved from LEFT — LEFT/RIGHT are the global
            // tab cycle now).
            if (shrine_key_pressed(BTN_A)) {
                memset(s_nodes, 0, sizeof(s_nodes));
                s_node_count = 0;
                s_raw_packets = 0;
                s_header_ok = 0;
                shrine_beep(600, 30);
                render_scan();
                continue;
            }
            if (shrine_key_pressed(BTN_B)) {
                if (send_meshtastic_nodeinfo()) {
                    last_nodeinfo_ms = shrine_ms();
                    shrine_beep(1800, 60);
                } else {
                    shrine_beep(300, 60);
                }
                render_scan();
                continue;
            }
            // Repaint ~2x/sec so ages tick and new packets show up
            // without needing button input.
            static uint32_t last_scan_paint;
            uint32_t now_ms = shrine_ms();
            if (now_ms - last_scan_paint > 500) {
                last_scan_paint = now_ms;
                render_scan();
            }
            break;
        }
        }

        // Repaint the RX flash overlay each tick while it's active so
        // the blinking border animates. draw_rx_flash_if_active
        // internally checks the expiry timestamp — this is a cheap
        // no-op most of the time.
        if (s_rx_flash_until) {
            // Force a fresh mode render underneath, then draw the flash
            // on top, so exiting the popup restores the panel cleanly.
            switch (mode) {
                case UI_COMPOSE: render_compose(radio_ok); break;
                case UI_PICK:    render_pick(cur, radio_ok, msg_n); break;
                case UI_INBOX:   render_inbox(inbox_scroll); break;
                case UI_SCAN:    render_scan(); break;
                default: break;
            }
            draw_rx_flash_if_active();
        }

        shrine_sleep_ms(30);
    }
}
