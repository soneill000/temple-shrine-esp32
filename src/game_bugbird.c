// game_bugbird.c — literal port of Terry Davis's TempleOS BugBird.
//
// Terry's game: bird fixed at bird_x=100, gravity pulls it down (+0.15
// per tick), SPACE flaps it up (impulse tt=-0.005, decays toward 0
// while bird_y += 75*tt). 32 bugs scattered at random; the world
// scrolls (frame_x decreases 0.1/tick). Eat a bug when the bird
// overlaps a bug within a 10x10 box. Game ends when all bugs eaten;
// best time persists.
//
// Sprites (Terry's own from bugbird.cpp.z's tail — 4 vector sprites):
//   BI=1  bird resting / base pose
//   BI=2  bird wings-up (flapping)
//   BI=3  bird eating - mouth open
//   BI=4  bird eating - mouth closed
// Terry uses SpriteInterpolate to blend between poses; we pick the
// nearest by flap_phase / eat state — same policy as everywhere else.
//
// Controls:
//   A / SPACE  flap
//   B          restart
//   BOOT       exit

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"
#include "templeshim.h"

#include "sprite_bugbird.h"

#include "scene_fb.h"
#define s_fb g_scene_fb

#include <stdio.h>
#include <string.h>
#include <math.h>

// Terry's per-tick constants ran at TempleOS's AnimateTask rate. On our
// 30 fps loop they were imperceptible (flap moved bird ~0.4 px/tick,
// gravity was ~4 px/sec). Rewritten to time-based physics (dt seconds)
// so a flap is a proper impulse and the game actually plays.
#define BB_BORDER      5
#define BB_EAT_TIME    500       // ms
#define BB_BIRD_BOX    14        // hit radius (scaled sprite is bigger)
#define BB_MAX_BUGS    32
#define BB_SPRITE_SCALE  2.0f    // draw 2x so bird is readable on 320x240
#define BB_GRAVITY     380.0f    // px/s^2
#define BB_FLAP_VY    -180.0f    // instant upward velocity on A press
#define BB_SCROLL_VX    60.0f    // world scroll speed (px/s)
#define BB_VY_MAX      260.0f    // terminal fall velocity

// Fixed screen dims (badge fb is 320x240 already; Terry's task is
// half-screen so we treat the whole fb as the playfield).
#define BB_W SCREEN_W
#define BB_H SCREEN_H

typedef struct {
    int16_t x, y;
    bool    dead;
} bb_bug_t;

static bb_bug_t s_bugs[BB_MAX_BUGS];
static int      s_bug_cnt;
static float    s_bird_x = 100.0f;   // Terry: fixed at x=100
static float    s_bird_y = 40.0f;
static float    s_bird_vy = 0.0f;    // px/sec vertical velocity
static float    s_frame_x = 0.0f;
static uint32_t s_last_ms = 0;
static uint32_t s_flap_flash_until = 0;  // "wings up" pose for this window
static uint32_t s_eat_timeout_ms = 0;
static uint32_t s_game_t0_ms = 0;
static uint32_t s_game_tf_ms = 0;
static float    s_best_score = 99.99f;

// ---- fb helpers ----
static inline uint16_t rgb(color_t c) { return PAL_RGB565[c & 15]; }
static inline void fb_pixel(int x, int y, color_t c)
{
    if ((unsigned)x < (unsigned)BB_W && (unsigned)y < (unsigned)BB_H)
        s_fb[y * BB_W + x] = rgb(c);
}
static void fb_fill_rect(int x, int y, int w, int h, color_t c)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > BB_W) w = BB_W - x;
    if (y + h > BB_H) h = BB_H - y;
    if (w <= 0 || h <= 0) return;
    uint16_t v = rgb(c);
    for (int j = 0; j < h; j++) {
        uint16_t *p = &s_fb[(y + j) * BB_W + x];
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
static void fb_puts(int x, int y, const char *s, color_t fg, color_t bg)
{
    while (*s) { fb_putc(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static inline void fb_clear(color_t c)
{
    uint16_t v = rgb(c);
    int n = BB_W * BB_H;
    for (int i = 0; i < n; i++) s_fb[i] = v;
}

static void bb_init(void)
{
    s_bird_x = 100.0f;
    s_bird_y = BB_H / 2.0f;
    s_bird_vy = 0.0f;
    s_frame_x = 0.0f;
    s_flap_flash_until = 0;
    s_eat_timeout_ms = 0;
    s_bug_cnt = BB_MAX_BUGS;
    for (int i = 0; i < BB_MAX_BUGS; i++) {
        s_bugs[i].dead = false;
        s_bugs[i].x = (int16_t)shrine_god(0x10000);
        s_bugs[i].y = (int16_t)shrine_god(0x10000);
    }
    s_last_ms = shrine_ms();
    s_game_t0_ms = s_last_ms;
    s_game_tf_ms = 0;
}

// Pick which sprite frame to show. Terry uses SpriteInterpolate between
// BI=1 (base) and BI=2 (wings-up), then BI=3/BI=4 for the eating flash.
// We pick nearest: wings-up for a short window after each flap press.
static const uint8_t *bb_current_sprite(unsigned *size_out, uint32_t now)
{
    bool up = (now < s_flap_flash_until);
    const uint8_t *base = up ? SPRITE_BUGBIRD_BI_2 : SPRITE_BUGBIRD_BI_1;
    unsigned bsz = up ? SPRITE_BUGBIRD_BI_2_SIZE : SPRITE_BUGBIRD_BI_1_SIZE;

    if (s_eat_timeout_ms && now < s_eat_timeout_ms) {
        // Eating flash — alternate BI=3 / BI=4 every 50 ms.
        bool alt = ((now / 50) & 1) != 0;
        base = alt ? SPRITE_BUGBIRD_BI_4 : SPRITE_BUGBIRD_BI_3;
        bsz  = alt ? SPRITE_BUGBIRD_BI_4_SIZE : SPRITE_BUGBIRD_BI_3_SIZE;
    }
    *size_out = bsz;
    return base;
}

static void bb_check_bugs(uint32_t now)
{
    int h = BB_W - 2 * BB_BORDER;
    int v = BB_H - 2 * BB_BORDER;
    for (int i = 0; i < BB_MAX_BUGS; i++) {
        if (s_bugs[i].dead) continue;
        int x = ((int)s_bugs[i].x + (int)s_frame_x) % h;
        if (x < 0) x += h;
        x += BB_BORDER;
        int y = ((int)s_bugs[i].y) % v;
        if (y < 0) y += v;
        y += BB_BORDER;
        int dx = x - (int)s_bird_x;
        int dy = y - (int)s_bird_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < BB_BIRD_BOX && dy < BB_BIRD_BOX) {
            s_bugs[i].dead = true;
            s_eat_timeout_ms = now + BB_EAT_TIME;
            shrine_beep(1000, 40);
            s_bug_cnt--;
        }
    }
    if (!s_game_tf_ms && s_bug_cnt == 0) {
        s_game_tf_ms = now;
        float t = (float)(s_game_tf_ms - s_game_t0_ms) / 1000.0f;
        if (t < s_best_score) s_best_score = t;
        shrine_beep(2000, 100);
        shrine_beep(2400, 100);
        shrine_beep(2800, 200);
    }
}

// Time-based physics — same spirit as Terry's AnimateTask (gravity always
// pulling down, flap = instant upward impulse) but with dt in seconds so
// it plays sensibly at the badge's 30-50 fps.
static void bb_tick(bool flap_pressed, uint32_t now, float dt)
{
    if (flap_pressed) {
        s_bird_vy = BB_FLAP_VY;
        s_flap_flash_until = now + 120;   // wings-up pose for 120 ms
        shrine_beep(1400, 20);
    }
    s_bird_vy += BB_GRAVITY * dt;
    if (s_bird_vy > BB_VY_MAX) s_bird_vy = BB_VY_MAX;
    s_bird_y  += s_bird_vy * dt;
    if (s_bird_y < BB_BORDER) {
        s_bird_y = BB_BORDER;
        if (s_bird_vy < 0) s_bird_vy = 0;
    }
    if (s_bird_y > BB_H - BB_BORDER) {
        s_bird_y = BB_H - BB_BORDER;
        if (s_bird_vy > 0) s_bird_vy = 0;
    }
    s_frame_x -= BB_SCROLL_VX * dt;
    // Wrap to keep frame_x in a reasonable range (world width).
    int h = BB_W - 2 * BB_BORDER;
    while (s_frame_x < -h) s_frame_x += h;
    while (s_frame_x >  h) s_frame_x -= h;
    bb_check_bugs(now);
}

// ---- Draw ----
static void bb_draw(uint32_t now)
{
    // Terry doesn't set a bg color explicitly for BugBird — the doc's
    // default text_attr YELLOW<<4+BLUE means yellow bg + blue text.
    // We use black bg for night-flying aesthetic; feels close to how
    // it renders on Terry's monitor with the sprite silhouette.
    fb_clear(C_BG);

    // Bugs — Terry: BLACK dot + LTPURPLE dot at y-1.
    int h = BB_W - 2 * BB_BORDER;
    int v = BB_H - 2 * BB_BORDER;
    for (int i = 0; i < BB_MAX_BUGS; i++) {
        if (s_bugs[i].dead) continue;
        int x = ((int)s_bugs[i].x + (int)s_frame_x) % h;
        if (x < 0) x += h;
        x += BB_BORDER;
        int y = ((int)s_bugs[i].y) % v;
        if (y < 0) y += v;
        y += BB_BORDER;
        fb_pixel(x,     y,     C_BLACK);
        fb_pixel(x + 1, y,     C_BLACK);
        fb_pixel(x,     y - 1, C_LTMAGENTA);
    }

    // Bird sprite via Sprite3S — scaled up so Terry's ~20 px vector art
    // is actually readable on our 320x240 panel. Its own transparent
    // pixels (0xFF) let the black bg show through so there's no box
    // artifact around the sprite.
    unsigned sz;
    const uint8_t *img = bb_current_sprite(&sz, now);
    Sprite3S(&gr_dc, (int)s_bird_x, (int)s_bird_y, 0, BB_SPRITE_SCALE, img, sz);

    // HUD — Terry: "Bugs:%.1f%% Time:%.2f Best:%.2f".
    char buf[64];
    float pct = 100.0f * (BB_MAX_BUGS - s_bug_cnt) / (float)BB_MAX_BUGS;
    float tsec = s_game_tf_ms
                 ? (float)(s_game_tf_ms - s_game_t0_ms) / 1000.0f
                 : (float)(now - s_game_t0_ms) / 1000.0f;
    snprintf(buf, sizeof(buf), "Bugs:%3d%% T:%.1f Best:%.1f",
             (int)pct, (double)tsec, (double)s_best_score);
    fb_puts(0, 0, buf,
            s_game_tf_ms ? C_LTRED : C_LTGREEN, C_BG);

    if (s_game_tf_ms) {
        // Terry: "Game Completed" flash.
        if (((now / 300) & 1)) {
            fb_puts((BB_W - 14 * GLYPH_W) / 2, BB_H / 2 - 4,
                    "Game Completed", C_LTRED, C_BG);
        }
    }

    display_present_full(s_fb);
}

// ---- Main loop ----
void game_bugbird_run(void)
{
    CDCInit(s_fb, BB_W, BB_H, 1);
    bb_init();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        // B = restart. BOOT quits.
        if (shrine_key_pressed(BTN_B)) bb_init();

        // A press = flap impulse. Edge-triggered by shrine's input layer.
        bool flap_pressed = shrine_key_pressed(BTN_A);

        uint32_t now = shrine_ms();
        float dt = (now - s_last_ms) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        if (dt <= 0.0f) dt = 0.02f;
        s_last_ms = now;

        bb_tick(flap_pressed, now, dt);
        bb_draw(now);

        shrine_sleep_ms(20);
    }
}
