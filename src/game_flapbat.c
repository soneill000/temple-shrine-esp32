// game_flapbat.c — port of Terry's FlapBat.HC.
//
// Terry's game: bat pinned to a fixed x, gravity pulls it down, SPACE
// flaps it up (impulse tapers if you flap rapidly), 32 glowing bugs
// scattered across a horizontally scrolling world. Eat all 32 as fast
// as possible.
//
// Our adaptation:
//   - Terry's sprite art (bat down / bat up / bat eating open / bat
//     eating closed) is replaced with procedural drawing since the
//     sprite decoder isn't wired yet (see sprite_decoder.h for the
//     start of that work).
//   - Everything else — flap impulse decay, gravity constant, glow
//     phase per bug, world scroll, eat timing, best score, win state —
//     ports one-to-one from FlapBat.HC.
//
// Controls:
//   A / UP    flap
//   BOOT      exit

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"
#include "sprite_decoder.h"
#include "flapbat_vector_sprites.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define BORDER     6
#define BUGS_NUM   32
#define BAT_BOX    12
#define EAT_TIME_S 0.5f
#define FLAP_TIME_S 0.5f
#define GLOW_PERIOD_S 3.0f

// --- Bat physics ---
static float s_bat_y;
static float s_bat_v;            // vertical velocity
static float s_flap_time_ms;     // last flap wall-clock
static float s_eat_until_ms;     // eat animation end
static uint32_t s_start_ms;
static uint32_t s_end_ms;        // 0 while playing
static float s_frame_x;          // horizontal scroll offset

// --- Bugs ---
typedef struct {
    int   x, y;                  // world coords (bugs' virtual x wraps)
    float glow_phase;
    bool  dead;
} bug_t;
static bug_t s_bugs[BUGS_NUM];
static int   s_bug_cnt;
static int   s_best_ms;

static inline int play_w(void) { return SCREEN_W - 2 * BORDER; }
static inline int play_h(void) { return SCREEN_H - 2 * BORDER; }
static inline int bat_x(void)  { return SCREEN_W / 8; }   // Terry's pix_width>>3

// --- Reset / spawn ---
static void reset_game(void)
{
    s_bat_y = SCREEN_H / 2;
    s_bat_v = 0;
    s_flap_time_ms = -1000.0f;
    s_eat_until_ms = 0;
    s_frame_x = 0;
    s_bug_cnt = BUGS_NUM;
    s_start_ms = shrine_ms();
    s_end_ms   = 0;
    for (int i = 0; i < BUGS_NUM; i++) {
        s_bugs[i].x = (int)shrine_god(play_w());
        s_bugs[i].y = (int)shrine_god(play_h());
        s_bugs[i].glow_phase = ((float)shrine_god(1000) / 1000.0f) * GLOW_PERIOD_S;
        s_bugs[i].dead = false;
    }
}

// --- Bitmap sprite renderer ---
// Blits a TempleOS palette-indexed bitmap through the shim. Composes into
// a stack buffer with BG fill for transparent pixels, then pushes as ONE
// display_blit. Previous version fired one SPI transaction per non-
// transparent pixel (~200 per bat draw); this version fires one.
static void blit_palette_bitmap(int cx, int cy, int w, int h,
                                const uint8_t *pixels)
{
    if (w <= 0 || h <= 0 || w > 48 || h > 48) return;
    uint16_t buf[48 * 48];
    uint16_t bg = PAL_RGB565[C_BG];
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint8_t p = pixels[row * w + col];
            buf[row * w + col] = (p == 0xFF) ? bg : PAL_RGB565[p & 15];
        }
    }
    display_blit(cx - w / 2, cy - h / 2, w, h, buf);
}

// --- Draw bat ---
// Terry's actual bat sprite, walked as a vector op stream through the
// sprite_decoder. Coordinates are signed offsets from (x, y) so the bat
// centers on that point exactly like Terry's Sprite3(dc, x, y, sprite).
static void draw_bat(int x, int y, float vabs, bool eating)
{
    (void)vabs;
    sprite_render_stream(sprite_flapbat_1, sizeof(sprite_flapbat_1), x, y);
    if (eating) {
        shrine_fill_rect(x + 6, y - 1, 3, 3, C_LTRED);
        shrine_pixel(x + 7, y - 2, C_YELLOW);
    }
}

// --- Draw one bug ---
static void draw_bug(int x, int y, float t_s, float glow_phase)
{
    float sawv = fmodf(t_s + glow_phase, GLOW_PERIOD_S) / GLOW_PERIOD_S;
    color_t c;
    if (sawv < 0.2f) {
        c = ((int)(sawv * 100) & 1) ? C_YELLOW : C_LTGREEN;
    } else {
        c = C_BLACK;
    }
    shrine_pixel(x,     y,     c);
    shrine_pixel(x + 1, y,     c);
    shrine_pixel(x,     y - 1, C_BLACK);
}

// --- HUD ---
static void draw_hud(uint32_t now_ms)
{
    char buf[48];
    int elapsed_ms = (int)((s_end_ms ? s_end_ms : now_ms) - s_start_ms);
    float pct = 100.0f * (BUGS_NUM - s_bug_cnt) / BUGS_NUM;
    int best_s10 = s_best_ms / 100;
    snprintf(buf, sizeof(buf),
             "BUGS %2d%%  T %3d.%01d  BEST %3d.%01d",
             (int)pct, elapsed_ms / 1000, (elapsed_ms % 1000) / 100,
             best_s10 / 10, best_s10 % 10);
    shrine_fill_rect(0, 0, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts(0, 0, buf, C_LTGREEN, C_BG);
}

// --- Draw whole play area ---
static void draw_all(uint32_t now_ms)
{
    // Clear play area.
    shrine_fill_rect(0, GLYPH_H, SCREEN_W, SCREEN_H - GLYPH_H,
                     PAL_RGB565[C_BG]);
    // Frame border.
    shrine_rect(BORDER - 1, GLYPH_H + BORDER - 1,
                play_w() + 2, play_h() + 2 - GLYPH_H, C_LTGRAY);

    float t_s = (now_ms - s_start_ms) / 1000.0f;
    int w = play_w(), h = play_h();
    // Scroll offset — Terry decrements frame_x by 0.1 per tick; we do it
    // in the update step but wrap here for drawing.
    for (int i = 0; i < BUGS_NUM; i++) {
        if (s_bugs[i].dead) continue;
        int wx = (s_bugs[i].x + (int)s_frame_x) % w;
        if (wx < 0) wx += w;
        int x = wx + BORDER;
        int y = (s_bugs[i].y % h) + BORDER;
        draw_bug(x, y, t_s, s_bugs[i].glow_phase);
    }
    // Bat.
    bool eating = (float)now_ms < s_eat_until_ms;
    draw_bat(bat_x(), (int)s_bat_y, fabsf(s_bat_v), eating);
    // HUD.
    draw_hud(now_ms);
    // Win banner.
    if (s_end_ms) {
        shrine_puts_centered(TEXT_ROWS / 2, "*  GAME COMPLETED  *", C_LTRED, C_BG);
        shrine_puts_centered(TEXT_ROWS / 2 + 2, "PRESS A TO RETRY", C_YELLOW, C_BG);
    }
}

// --- Check collisions with bugs ---
static void check_bugs(uint32_t now_ms)
{
    int bx = bat_x();
    int w = play_w();
    for (int i = 0; i < BUGS_NUM; i++) {
        if (s_bugs[i].dead) continue;
        int wx = (s_bugs[i].x + (int)s_frame_x) % w;
        if (wx < 0) wx += w;
        int x = wx + BORDER;
        int y = (s_bugs[i].y % play_h()) + BORDER;
        if (abs(x - bx) < BAT_BOX && abs(y - (int)s_bat_y) < BAT_BOX) {
            s_bugs[i].dead = true;
            s_bug_cnt--;
            s_eat_until_ms = (float)now_ms + EAT_TIME_S * 1000.0f;
            shrine_beep(1800, 40);
            shrine_beep(2200, 40);
        }
    }
    if (s_bug_cnt == 0 && !s_end_ms) {
        s_end_ms = now_ms;
        int elapsed = (int)(s_end_ms - s_start_ms);
        if (s_best_ms == 0 || elapsed < s_best_ms) s_best_ms = elapsed;
        shrine_beep(2400, 100);
        shrine_beep(2800, 100);
        shrine_beep(3200, 200);
    }
}

void game_flapbat_run(void)
{
    s_best_ms = 0;
    reset_game();
    shrine_clear(C_BG);
    draw_all(shrine_ms());

    // Wait for start.
    shrine_puts_centered(TEXT_ROWS / 2, "PRESS A TO BEGIN", C_YELLOW, C_BG);
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A) || shrine_key_pressed(BTN_UP)) {
            s_start_ms = shrine_ms();
            s_bat_v = -120.0f;
            s_flap_time_ms = (float)s_start_ms;
            shrine_beep(1400, 30);
            break;
        }
        shrine_sleep_ms(30);
    }

    uint32_t last = shrine_ms();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        uint32_t now = shrine_ms();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        last = now;

        if (!s_end_ms) {
            // Physics in real px/s. Flap sets upward velocity; gravity
            // pulls back. Terry's diminishing-returns rule kept us from
            // spamming — we keep it but as a small taper, not a hard
            // multiply, so first flaps actually work.
            if (shrine_key_pressed(BTN_A) || shrine_key_pressed(BTN_UP)) {
                float since = (now - s_flap_time_ms) / 1000.0f;
                float k = since / FLAP_TIME_S;
                if (k > 1.0f) k = 1.0f;
                // Base impulse always fires; k tapers a bonus.
                s_bat_v = -90.0f - 40.0f * k;   // -90 to -130 px/s
                s_flap_time_ms = (float)now;
                shrine_beep(1600, 15);
            }

            // Gravity + terminal fall.
            s_bat_v += 260.0f * dt;
            if (s_bat_v >  220.0f) s_bat_v =  220.0f;
            if (s_bat_v < -180.0f) s_bat_v = -180.0f;
            s_bat_y += s_bat_v * dt;
            if (s_bat_y < GLYPH_H + BORDER) {
                s_bat_y = GLYPH_H + BORDER;
                if (s_bat_v < 0) s_bat_v = 0;
            }
            if (s_bat_y > SCREEN_H - BORDER) {
                s_bat_y = SCREEN_H - BORDER;
                if (s_bat_v > 0) s_bat_v = 0;
            }

            // World scroll.
            s_frame_x -= 60.0f * dt;
            int w = play_w();
            while (s_frame_x < 0) s_frame_x += w;

            check_bugs(now);
        } else {
            if (shrine_key_pressed(BTN_A) || shrine_key_pressed(BTN_UP)) {
                reset_game();
            }
        }

        draw_all(now);
        shrine_sleep_ms(30);
    }
}
