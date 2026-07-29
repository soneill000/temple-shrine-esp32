// game_bombergolf.c — port of Terry Davis's BomberGolf.HC using his own
// sprites and the buffered scene_fb pipeline.
//
// Terry's original is a top-down bomber over a Mat4x4-rotated world. Player
// controls heading (LEFT/RIGHT) and throttle (UP/DOWN); drops bombs with
// SPACE. Ten targets (bunkers + tanks) to smite. Score = number of key-
// strokes used (lower is better).
//
// Adaptations for the badge:
//  - Reduced target count (6) so a round fits the badge attention span.
//  - Terry rotates the whole world (Mat4x4RotZ) so the plane always faces
//    up on screen. We do the same by rotating world→screen coords by
//    -theta before drawing.
//  - Sprites are Terry's own from BomberGolf.HC's DolDoc tail (extracted
//    via tools/extract_sprite_tail.py — see sprite_bombergolf.h).
//    Sprite bank:
//      BI=1  71x56  bomber plane (top-down)
//      BI=2  28x31  tank alive
//      BI=3  22x16  tree
//      BI=4  31x18  bunker alive
//      BI=5  35x18  bunker wrecked
//      BI=6  24x29  tank wrecked
//      BI=7  30x23  spare building (unused for now)
//  - Whole frame composes into g_scene_fb (PSRAM); one display_present_full
//    per frame. Fixes the "flickering and unplayable" per-SPI-call frame
//    that the previous procedural version had.
//
// Controls:
//   LEFT / RIGHT   turn
//   UP   / DOWN    throttle
//   A              drop bomb
//   B              restart
//   BOOT           exit

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"
#include "templeshim.h"

#include "sprite_bombergolf.h"

#include "scene_fb.h"
#define s_fb g_scene_fb

#include <stdio.h>
#include <string.h>
#include <math.h>

#define WORLD_W       400
#define WORLD_H       400
#define N_TARGETS     6
#define N_BUNKERS     2
#define N_TREES       18
#define MAX_BOMBS     4
#define FALL_TIME_MS  1500
#define EXPLODE_MS    350
#define BOMB_KILL_R2  (28.0f * 28.0f)
#define TANK_V        14.0f
#define V_MIN         20.0f
#define V_MAX         180.0f
#define PLAY_TOP      16
#define PLAY_BOT      216

typedef struct {
    float x, y, theta;
    bool  alive;
    bool  is_tank;
} target_t;

typedef struct {
    float x, y;
    uint32_t t_dropped;
    bool  active;
    bool  exploding;
} bomb_t;

typedef struct { int x, y; } tree_t;

static float  s_px, s_py;         // plane world position
static float  s_theta;             // current heading (radians, 0 = up/-y)
static float  s_theta_f;           // target heading (damped follow)
static float  s_v;                 // velocity (world units per second)
static int    s_key_cnt;
static int    s_kills;
static int    s_best;
static target_t s_targets[N_TARGETS];
static bomb_t   s_bombs[MAX_BOMBS];
static tree_t   s_trees[N_TREES];
static uint32_t s_last_ms;

// --- fb helpers (identical pattern to game_bugbird / game_squirt) --------
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
static inline void fb_hline(int x, int y, int w, color_t c) { fb_fill_rect(x, y, w, 1, c); }
static inline void fb_vline(int x, int y, int h, color_t c) { fb_fill_rect(x, y, 1, h, c); }
static void fb_rect(int x, int y, int w, int h, color_t c)
{
    fb_hline(x,         y,         w, c);
    fb_hline(x,         y + h - 1, w, c);
    fb_vline(x,         y,         h, c);
    fb_vline(x + w - 1, y,         h, c);
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

// --- World -> screen (with world rotation so plane always faces up) ------

static inline void world_to_screen(float wx, float wy, int *sxp, int *syp)
{
    float dx = wx - s_px;
    float dy = wy - s_py;
    // Rotate world by -theta so the plane's heading vector becomes screen-up.
    // With Terry's convention (theta=0 => moving -y in world), the plane's
    // heading unit vector is (sin θ, -cos θ). To make that vector map to
    // screen-up (0, -1) we rotate by -θ:
    //   x' =  dx*cos θ + dy*sin θ
    //   y' = -dx*sin θ + dy*cos θ
    float ct = cosf(s_theta), st = sinf(s_theta);
    float rx =  dx * ct + dy * st;
    float ry = -dx * st + dy * ct;
    *sxp = (int)(rx + SCREEN_W / 2);
    *syp = (int)(ry + (PLAY_TOP + PLAY_BOT) / 2);
}

static inline bool on_screen(int sx, int sy, int pad)
{
    return sx >= -pad && sx < SCREEN_W + pad
        && sy >= PLAY_TOP - pad && sy < PLAY_BOT + pad;
}

// --- Sprite draws --------------------------------------------------------

static void draw_tree(int sx, int sy)
{
    if (!on_screen(sx, sy, 20)) return;
    Sprite3(&gr_dc, sx, sy, 0,
            SPRITE_BOMBERGOLF_BI_3, SPRITE_BOMBERGOLF_BI_3_SIZE);
}

static void draw_bunker(int sx, int sy, bool alive)
{
    if (!on_screen(sx, sy, 24)) return;
    if (alive) {
        Sprite3(&gr_dc, sx, sy, 0,
                SPRITE_BOMBERGOLF_BI_4, SPRITE_BOMBERGOLF_BI_4_SIZE);
    } else {
        Sprite3(&gr_dc, sx, sy, 0,
                SPRITE_BOMBERGOLF_BI_5, SPRITE_BOMBERGOLF_BI_5_SIZE);
    }
}

static void draw_tank(int sx, int sy, bool alive)
{
    if (!on_screen(sx, sy, 24)) return;
    if (alive) {
        Sprite3(&gr_dc, sx, sy, 0,
                SPRITE_BOMBERGOLF_BI_2, SPRITE_BOMBERGOLF_BI_2_SIZE);
    } else {
        Sprite3(&gr_dc, sx, sy, 0,
                SPRITE_BOMBERGOLF_BI_6, SPRITE_BOMBERGOLF_BI_6_SIZE);
    }
}

static void draw_bomb(int sx, int sy, uint32_t age_ms)
{
    if (!on_screen(sx, sy, 12)) return;
    if (age_ms < FALL_TIME_MS) {
        // Shadow on ground + a rising dot above (shrinks as it "falls").
        fb_fill_circle(sx, sy, 3, C_DKGRAY);
        float t = (float)age_ms / FALL_TIME_MS;   // 0..1
        int rise = (int)(14.0f * (1.0f - t));     // px above shadow
        fb_fill_rect(sx - 1, sy - rise - 1, 3, 3, C_LTRED);
    } else {
        // Explosion — yellow ring, red core, white pinpoint.
        int age = age_ms - FALL_TIME_MS;
        int r = 5 + age / 20;
        if (r > 14) r = 14;
        fb_fill_circle(sx, sy, r,     C_YELLOW);
        fb_fill_circle(sx, sy, r - 2, C_LTRED);
        fb_pixel(sx, sy, C_WHITE);
    }
}

static void draw_plane_center(void)
{
    // Plane sits at screen center pointing up; world rotates around it.
    Sprite3(&gr_dc, SCREEN_W / 2, (PLAY_TOP + PLAY_BOT) / 2, 0,
            SPRITE_BOMBERGOLF_BI_1, SPRITE_BOMBERGOLF_BI_1_SIZE);
}

// --- World border --------------------------------------------------------

static void draw_border(void)
{
    // Border is a rotated rectangle. Draw as 4 line segments in screen
    // space after applying world_to_screen to each corner.
    int cx[4], cy[4];
    world_to_screen(0,        0,        &cx[0], &cy[0]);
    world_to_screen(WORLD_W,  0,        &cx[1], &cy[1]);
    world_to_screen(WORLD_W,  WORLD_H,  &cx[2], &cy[2]);
    world_to_screen(0,        WORLD_H,  &cx[3], &cy[3]);
    gr_dc.color = C_YELLOW;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        GrLine(&gr_dc, cx[i], cy[i], cx[j], cy[j]);
    }
}

// --- HUD -----------------------------------------------------------------

static void draw_hud(void)
{
    char buf[48];
    fb_fill_rect(0, 0, SCREEN_W, PLAY_TOP, PAL_RGB565[C_BG]);
    fb_fill_rect(0, PLAY_BOT, SCREEN_W, SCREEN_H - PLAY_BOT, PAL_RGB565[C_BG]);
    snprintf(buf, sizeof(buf), "KILL %d/%d  KEY %04d  BEST %04d",
             s_kills, N_TARGETS, s_key_cnt,
             s_best < 9999 ? s_best : 9999);
    fb_puts(0, 0, buf, C_WHITE, C_BG);
    // Speed bar (bottom).
    int bar_full = (int)((s_v - V_MIN) / (V_MAX - V_MIN) * 60);
    if (bar_full < 0) bar_full = 0;
    if (bar_full > 60) bar_full = 60;
    fb_puts(0, TEXT_ROWS - 2, "SPD [", C_LTCYAN, C_BG);
    fb_fill_rect(5 * GLYPH_W, (TEXT_ROWS - 2) * GLYPH_H + 2,
                 60, 4, C_DKGRAY);
    fb_fill_rect(5 * GLYPH_W, (TEXT_ROWS - 2) * GLYPH_H + 2,
                 bar_full, 4, C_LTGREEN);
    fb_puts(5 + 60 / GLYPH_W + 1, TEXT_ROWS - 2, "]", C_LTCYAN, C_BG);
    fb_puts(15, TEXT_ROWS - 2, "L/R TURN U/D SPD A BOMB",
            C_LTGRAY, C_BG);
}

// --- Sim -----------------------------------------------------------------

static void reset(void)
{
    s_px = WORLD_W / 2.0f;
    s_py = WORLD_H - 40.0f;
    s_theta = s_theta_f = 0.0f;   // pointing up (-y)
    s_v = 60.0f;
    s_key_cnt = 0;
    s_kills = 0;
    for (int i = 0; i < N_TREES; i++) {
        s_trees[i].x = (int)shrine_god(WORLD_W - 20) + 10;
        s_trees[i].y = (int)shrine_god(WORLD_H - 20) + 10;
    }
    for (int i = 0; i < N_TARGETS; i++) {
        s_targets[i].x = (float)((int)shrine_god(WORLD_W - 80) + 40);
        s_targets[i].y = (float)((int)shrine_god(WORLD_H - 80) + 40);
        s_targets[i].alive = true;
        s_targets[i].is_tank = (i >= N_BUNKERS);
        s_targets[i].theta = (float)shrine_god(628) / 100.0f;   // 0..2π
    }
    for (int i = 0; i < MAX_BOMBS; i++) s_bombs[i].active = false;
    s_last_ms = shrine_ms();
}

static void drop_bomb(void)
{
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!s_bombs[i].active) {
            // Lead — bomb lands where plane will be at fall_time.
            float lead = 0.75f * (FALL_TIME_MS / 1000.0f) * s_v;
            s_bombs[i].x = s_px + lead * sinf(s_theta);
            s_bombs[i].y = s_py - lead * cosf(s_theta);
            s_bombs[i].t_dropped = shrine_ms();
            s_bombs[i].active = true;
            s_bombs[i].exploding = false;
            shrine_beep(600, 40);
            return;
        }
    }
}

static void update(float dt)
{
    // Turn toward target heading (damped follow).
    s_theta += (s_theta_f - s_theta) * dt * 3.0f;
    // Move.
    s_px += s_v * sinf(s_theta) * dt;
    s_py -= s_v * cosf(s_theta) * dt;
    // Clamp to world.
    if (s_px < 8)          s_px = 8;
    if (s_px > WORLD_W - 8) s_px = WORLD_W - 8;
    if (s_py < 8)          s_py = 8;
    if (s_py > WORLD_H - 8) s_py = WORLD_H - 8;

    // Move tanks.
    for (int i = 0; i < N_TARGETS; i++) {
        if (!s_targets[i].alive || !s_targets[i].is_tank) continue;
        s_targets[i].x += TANK_V * sinf(s_targets[i].theta) * dt;
        s_targets[i].y -= TANK_V * cosf(s_targets[i].theta) * dt;
        s_targets[i].theta += (i & 1 ? +1 : -1) * dt * 0.4f;
        if (s_targets[i].x < 8) { s_targets[i].x = 8; s_targets[i].theta = -s_targets[i].theta; }
        if (s_targets[i].x > WORLD_W - 8) { s_targets[i].x = WORLD_W - 8; s_targets[i].theta = -s_targets[i].theta; }
        if (s_targets[i].y < 8) { s_targets[i].y = 8; s_targets[i].theta = 3.14159f - s_targets[i].theta; }
        if (s_targets[i].y > WORLD_H - 8) { s_targets[i].y = WORLD_H - 8; s_targets[i].theta = 3.14159f - s_targets[i].theta; }
    }

    // Bombs.
    uint32_t now = shrine_ms();
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!s_bombs[i].active) continue;
        uint32_t age = now - s_bombs[i].t_dropped;
        if (age >= FALL_TIME_MS && !s_bombs[i].exploding) {
            s_bombs[i].exploding = true;
            shrine_beep(200, 80);
            for (int j = 0; j < N_TARGETS; j++) {
                if (!s_targets[j].alive) continue;
                float dx = s_targets[j].x - s_bombs[i].x;
                float dy = s_targets[j].y - s_bombs[i].y;
                if (dx * dx + dy * dy <= BOMB_KILL_R2) {
                    s_targets[j].alive = false;
                    s_kills++;
                    shrine_beep(1600, 60);
                    shrine_beep(2000, 60);
                }
            }
        }
        if (age >= FALL_TIME_MS + EXPLODE_MS) s_bombs[i].active = false;
    }
}

static void draw_all(uint32_t t_ms)
{
    // Play area background: green grass; sky is HUD color at top/bottom.
    fb_fill_rect(0, PLAY_TOP, SCREEN_W, PLAY_BOT - PLAY_TOP, PAL_RGB565[C_GREEN]);
    draw_border();
    for (int i = 0; i < N_TREES; i++) {
        int sx, sy;
        world_to_screen((float)s_trees[i].x, (float)s_trees[i].y, &sx, &sy);
        draw_tree(sx, sy);
    }
    for (int i = 0; i < N_TARGETS; i++) {
        int sx, sy;
        world_to_screen(s_targets[i].x, s_targets[i].y, &sx, &sy);
        if (s_targets[i].is_tank) draw_tank(sx, sy, s_targets[i].alive);
        else                       draw_bunker(sx, sy, s_targets[i].alive);
    }
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!s_bombs[i].active) continue;
        int sx, sy;
        world_to_screen(s_bombs[i].x, s_bombs[i].y, &sx, &sy);
        draw_bomb(sx, sy, t_ms - s_bombs[i].t_dropped);
    }
    draw_plane_center();
    draw_hud();
    display_present_full(s_fb);
}

void game_bombergolf_run(void)
{
    // Templeshim points at scene_fb at 1:1 scale so Sprite3 lands sprites
    // in fb-native coords (no Terry-space halving needed here).
    CDCInit(s_fb, SCREEN_W, SCREEN_H, 1);
    reset();
    s_best = 9999;

    bool round_won = false;
    uint32_t win_at = 0;
    (void)win_at;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (!round_won) {
            if (shrine_key_pressed(BTN_UP))    { s_v += 15; if (s_v > V_MAX) s_v = V_MAX; s_key_cnt++; }
            if (shrine_key_pressed(BTN_DOWN))  { s_v -= 15; if (s_v < V_MIN) s_v = V_MIN; s_key_cnt++; }
            if (shrine_key_pressed(BTN_LEFT))  { s_theta_f -= 0.35f; s_key_cnt++; }
            if (shrine_key_pressed(BTN_RIGHT)) { s_theta_f += 0.35f; s_key_cnt++; }
            if (shrine_key_pressed(BTN_A))     { drop_bomb();        s_key_cnt++; }
        } else {
            if (shrine_key_pressed(BTN_A) || shrine_key_pressed(BTN_B)) {
                reset();
                round_won = false;
                continue;
            }
        }
        if (shrine_key_pressed(BTN_B) && !round_won) {
            reset();
            continue;
        }

        uint32_t now = shrine_ms();
        float dt = (now - s_last_ms) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        s_last_ms = now;

        if (!round_won) update(dt);

        draw_all(now);

        if (!round_won && s_kills >= N_TARGETS) {
            round_won = true;
            win_at = now;
            if (s_key_cnt < s_best) s_best = s_key_cnt;
            shrine_beep(1600, 100); shrine_beep(2000, 100); shrine_beep(2400, 200);
        }
        if (round_won) {
            // Overlay the "GAME COMPLETED" banner on top of the composed
            // frame. draw_all already blitted once above; overlay + one
            // extra present is fine here since this is only per-frame
            // after victory.
            char buf[32];
            fb_puts_centered(12, "* GAME COMPLETED *", C_YELLOW, C_BG);
            snprintf(buf, sizeof(buf), "KEYS: %d   BEST: %d", s_key_cnt, s_best);
            fb_puts_centered(14, buf, C_WHITE, C_BG);
            fb_puts_centered(16, "PRESS A TO PLAY AGAIN", C_LTGREEN, C_BG);
            display_present_full(s_fb);
        }

        shrine_sleep_ms(30);
    }
}
