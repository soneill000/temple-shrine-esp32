// game_bombergolf.c — port of Terry's BomberGolf.HC.
//
// Terry's original: a top-down bomber where you control a plane's heading
// and speed with cursor keys, drop bombs with SPACE, and try to smite ten
// stationary bunkers + moving tanks with the fewest keystrokes.
//
// Adaptations for the badge:
//  - Reduced world (400 wide) and fewer targets (6) to fit our tighter
//    render loop.
//  - No Mat4x4 rotation of the world — camera follows the plane, plane
//    sprite is drawn as a rotated triangle so you still see heading.
//  - Sprites are procedural primitives (triangles, rects) instead of the
//    embedded binary sprites from the .HC.
//  - Controls: UP/DOWN = throttle, LEFT/RIGHT = turn, A = bomb, BOOT = exit

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define WORLD_W       400
#define WORLD_H       400
#define N_TARGETS     6
#define N_BUNKERS     2
#define N_TREES       20
#define MAX_BOMBS     4
#define FALL_TIME_MS  2000
#define EXPLODE_MS    250
#define BOMB_KILL_R2  (24.0f * 24.0f)
#define TANK_V        14.0f
#define V_MIN         20.0f
#define V_MAX         180.0f
#define PLANE_SIZE    9.0f
#define PLAY_TOP      24
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
static float  s_theta;            // current heading (radians, 0 = up/-y)
static float  s_theta_f;          // target heading
static float  s_v;                // velocity (world units per second)
static int    s_key_cnt;
static int    s_kills;
static int    s_best;
static target_t s_targets[N_TARGETS];
static bomb_t   s_bombs[MAX_BOMBS];
static tree_t   s_trees[N_TREES];
static uint32_t s_last_ms;

// --- World-to-screen helpers ---

static inline int world_to_screen_x(float wx) { return (int)(wx - s_px + SCREEN_W / 2); }
static inline int world_to_screen_y(float wy) { return (int)(wy - s_py + (PLAY_TOP + PLAY_BOT) / 2); }
static inline bool on_screen(int sx, int sy, int pad) {
    return sx >= -pad && sx < SCREEN_W + pad
        && sy >= PLAY_TOP - pad && sy < PLAY_BOT + pad;
}

// --- Sprite helpers ---

static void draw_tree(int sx, int sy)
{
    if (!on_screen(sx, sy, 4)) return;
    shrine_fill_rect(sx - 1, sy - 2, 3, 4, C_GREEN);
    shrine_pixel(sx - 2, sy,     C_GREEN);
    shrine_pixel(sx + 2, sy,     C_GREEN);
    shrine_pixel(sx,     sy + 2, C_BROWN);   // trunk
}

static void draw_bunker(int sx, int sy, bool alive, uint32_t t_ms)
{
    if (!on_screen(sx, sy, 6)) return;
    color_t body   = alive ? C_LTGRAY : C_DKGRAY;
    color_t accent = alive ? C_LTGREEN : C_LTRED;
    shrine_fill_rect(sx - 4, sy - 4, 9, 9, body);
    shrine_rect    (sx - 4, sy - 4, 9, 9, C_BLACK);
    shrine_fill_rect(sx - 1, sy - 1, 3, 3, accent);
    if (!alive && (t_ms / 200) & 1) {
        shrine_pixel(sx - 3, sy - 3, C_YELLOW);
        shrine_pixel(sx + 3, sy + 3, C_YELLOW);
    }
}

static void draw_tank(int sx, int sy, float theta, bool alive, uint32_t t_ms)
{
    if (!on_screen(sx, sy, 6)) return;
    color_t body   = alive ? C_BROWN   : C_DKGRAY;
    color_t barrel = alive ? C_YELLOW  : C_LTRED;
    shrine_fill_rect(sx - 3, sy - 3, 7, 7, body);
    shrine_rect    (sx - 3, sy - 3, 7, 7, C_BLACK);
    // Barrel points along theta.
    int bx = sx + (int)(6.0f * sinf(theta));
    int by = sy - (int)(6.0f * cosf(theta));
    shrine_line(sx, sy, bx, by, barrel);
    if (!alive && (t_ms / 200) & 1) shrine_pixel(sx, sy, C_YELLOW);
}

static void draw_bomb(int sx, int sy, uint32_t age_ms)
{
    if (!on_screen(sx, sy, 6)) return;
    // Falling: shadow on ground + a small dot above it that shrinks.
    if (age_ms < FALL_TIME_MS) {
        int shadow_r = 3;
        for (int i = -shadow_r; i <= shadow_r; i++)
            for (int j = -shadow_r; j <= shadow_r; j++)
                if (i*i + j*j <= shadow_r * shadow_r)
                    shrine_pixel(sx + i, sy + j, C_DKGRAY);
        float t = (float)age_ms / FALL_TIME_MS;   // 0..1
        int rise = (int)(12.0f * (1.0f - t));    // px above shadow
        shrine_fill_rect(sx - 1, sy - rise - 1, 3, 3, C_LTRED);
    } else {
        // Explosion.
        shrine_fill_rect(sx - 4, sy - 4, 9, 9, C_YELLOW);
        shrine_fill_rect(sx - 2, sy - 2, 5, 5, C_LTRED);
        shrine_pixel(sx, sy, C_WHITE);
    }
}

static void draw_plane(int sx, int sy, float theta)
{
    // Triangle: nose along +theta direction.
    float s = sinf(theta), c = cosf(theta);
    int nose_x = sx + (int)(PLANE_SIZE * s);
    int nose_y = sy - (int)(PLANE_SIZE * c);
    int lw_x   = sx - (int)(PLANE_SIZE * 0.7f * s) + (int)(PLANE_SIZE * 0.6f * c);
    int lw_y   = sy + (int)(PLANE_SIZE * 0.7f * c) + (int)(PLANE_SIZE * 0.6f * s);
    int rw_x   = sx - (int)(PLANE_SIZE * 0.7f * s) - (int)(PLANE_SIZE * 0.6f * c);
    int rw_y   = sy + (int)(PLANE_SIZE * 0.7f * c) - (int)(PLANE_SIZE * 0.6f * s);
    shrine_line(nose_x, nose_y, lw_x, lw_y, C_LTCYAN);
    shrine_line(nose_x, nose_y, rw_x, rw_y, C_LTCYAN);
    shrine_line(lw_x,   lw_y,   rw_x, rw_y, C_LTCYAN);
    shrine_fill_rect(sx - 1, sy - 1, 3, 3, C_YELLOW);
}

// --- World border ---

static void draw_border(void)
{
    int tlx = world_to_screen_x(0), tly = world_to_screen_y(0);
    shrine_rect(tlx, tly, WORLD_W, WORLD_H, C_YELLOW);
}

// --- HUD ---

static void draw_hud(void)
{
    char buf[48];
    // Speed bar at top.
    shrine_fill_rect(0, PLAY_BOT + 2, SCREEN_W, 8, PAL_RGB565[C_BG]);
    snprintf(buf, sizeof(buf), "KILL %d/%d  KEY %04d  BEST %04d",
             s_kills, N_TARGETS, s_key_cnt,
             s_best < 9999 ? s_best : 9999);
    shrine_puts(0, TEXT_ROWS - 3, buf, C_WHITE, C_BG);
    // Speed as a small bar.
    int bar_full = (int)((s_v - V_MIN) / (V_MAX - V_MIN) * 60);
    shrine_puts(0, TEXT_ROWS - 2, "SPD [", C_LTCYAN, C_BG);
    shrine_fill_rect(5 * GLYPH_W, (TEXT_ROWS - 2) * GLYPH_H + 2,
                     60, 4, PAL_RGB565[C_DKGRAY]);
    shrine_fill_rect(5 * GLYPH_W, (TEXT_ROWS - 2) * GLYPH_H + 2,
                     bar_full, 4, PAL_RGB565[C_LTGREEN]);
    shrine_puts(5 + 60 / GLYPH_W + 1, TEXT_ROWS - 2, "]", C_LTCYAN, C_BG);
    shrine_puts(15, TEXT_ROWS - 2, "L/R TURN  U/D SPD  A BOMB",
                C_LTGRAY, C_BG);
}

// --- Sim ---

static void reset(void)
{
    s_px = WORLD_W / 2.0f;
    s_py = WORLD_H - 40.0f;
    s_theta = s_theta_f = 0.0f;   // pointing up (-y)
    s_v = 40.0f;
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
            float lead = 0.8f * (FALL_TIME_MS / 1000.0f) * s_v;
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
    // Turn toward target heading (damped).
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
        // Wrap in world.
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
            // Check kills.
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
    // Play area background.
    shrine_fill_rect(0, PLAY_TOP, SCREEN_W, PLAY_BOT - PLAY_TOP,
                     PAL_RGB565[C_BG]);
    draw_border();
    for (int i = 0; i < N_TREES; i++) {
        draw_tree(world_to_screen_x(s_trees[i].x),
                  world_to_screen_y(s_trees[i].y));
    }
    for (int i = 0; i < N_TARGETS; i++) {
        int sx = world_to_screen_x(s_targets[i].x);
        int sy = world_to_screen_y(s_targets[i].y);
        if (s_targets[i].is_tank)
            draw_tank(sx, sy, s_targets[i].theta, s_targets[i].alive, t_ms);
        else
            draw_bunker(sx, sy, s_targets[i].alive, t_ms);
    }
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!s_bombs[i].active) continue;
        int sx = world_to_screen_x(s_bombs[i].x);
        int sy = world_to_screen_y(s_bombs[i].y);
        draw_bomb(sx, sy, t_ms - s_bombs[i].t_dropped);
    }
    draw_plane(SCREEN_W / 2, (PLAY_TOP + PLAY_BOT) / 2, s_theta);
}

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  BOMBERGOLF  *", C_YELLOW, C_BG);
    shrine_puts_centered(2, "SMITE THE TANKS AND BUNKERS", C_LTCYAN, C_BG);
    for (int c = 0; c < TEXT_COLS; c++) {
        shrine_putc(c, TEXT_ROWS - 4, G_HLINE[0], C_YELLOW, C_BG);
    }
}

void game_bombergolf_run(void)
{
    reset();
    s_best = 9999;
    draw_chrome();
    draw_hud();
    draw_all(shrine_ms());

    bool round_won = false;
    uint32_t win_at = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (!round_won) {
            if (shrine_key_pressed(BTN_UP))    { s_v += 15; if (s_v > V_MAX) s_v = V_MAX; s_key_cnt++; draw_hud(); }
            if (shrine_key_pressed(BTN_DOWN))  { s_v -= 15; if (s_v < V_MIN) s_v = V_MIN; s_key_cnt++; draw_hud(); }
            if (shrine_key_pressed(BTN_LEFT))  { s_theta_f -= 0.35f; s_key_cnt++; draw_hud(); }
            if (shrine_key_pressed(BTN_RIGHT)) { s_theta_f += 0.35f; s_key_cnt++; draw_hud(); }
            if (shrine_key_pressed(BTN_A))     { drop_bomb();       s_key_cnt++; draw_hud(); }
        } else {
            if (shrine_key_pressed(BTN_A)) {
                reset(); draw_chrome(); draw_hud(); draw_all(shrine_ms());
                round_won = false;
                continue;
            }
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
            shrine_puts_centered(12, "* GAME COMPLETED *", C_YELLOW, C_BG);
            char buf[32];
            snprintf(buf, sizeof(buf), "KEYS: %d   BEST: %d", s_key_cnt, s_best);
            shrine_puts_centered(14, buf, C_WHITE, C_BG);
            shrine_puts_centered(16, "PRESS A TO PLAY AGAIN", C_LTGREEN, C_BG);
        }

        shrine_sleep_ms(30);
    }
}
