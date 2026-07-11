// game_raindrops.c — port of Terry's RainDrops.HC.
//
// Terry's original: 8192 blue drops fall from the top of the screen and
// interact with a light-red frame structure. Drops draw themselves only
// where the underlying pixel is WHITE, so the red framework blocks them
// and the player toggles WHITE "gates" at fixed spots to let drops
// through.
//
// Our adaptation swaps his pixel-peek collision model for explicit
// segment collision (~fewer drops, but each one tests a small list of
// line/rect obstacles). Physics behavior is otherwise the same: gravity,
// a small random horizontal jitter, absorbed on hit or on ground.
//
// Controls:
//   A / B    open/close upper / lower gate
//   BOOT     exit

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define N_DROPS   240
#define PLAY_TOP  22
#define PLAY_BOT  (SCREEN_H - 12)

typedef struct { int x, y; int8_t vx, vy; bool active; } drop_t;
static drop_t s_drops[N_DROPS];
static int s_caught;
static bool s_gate_upper = true;    // start open
static bool s_gate_lower = false;   // start closed

// --- Frame geometry ---
// A funnel: two long walls from the sides meeting at a central column,
// a small square shelf below with a "cup" that catches drops. Player-
// toggleable openings sit in the middle of each wall.

#define GATE_UP_X1    140
#define GATE_UP_X2    180
#define GATE_UP_Y     100
#define GATE_LO_X1    130
#define GATE_LO_X2    190
#define GATE_LO_Y     170

static bool segment_hits_drop(int dx, int dy,
                              int x1, int y1, int x2, int y2)
{
    // Distance from point (dx, dy) to segment; approx via bounding box +
    // simple distance. Cheap check, adequate for gravity droplets.
    int minx = x1 < x2 ? x1 : x2;
    int maxx = x1 > x2 ? x1 : x2;
    int miny = y1 < y2 ? y1 : y2;
    int maxy = y1 > y2 ? y1 : y2;
    if (dx < minx - 1 || dx > maxx + 1) return false;
    if (dy < miny - 1 || dy > maxy + 1) return false;
    // Perpendicular distance from point to line.
    int a = y2 - y1, b = x1 - x2, c = x2 * y1 - x1 * y2;
    int num = a * dx + b * dy + c;
    if (num < 0) num = -num;
    int denom2 = a * a + b * b;
    if (denom2 == 0) return true;
    return (num * num) <= (denom2 * 2);   // within ~1.4 px
}

// Obstacles: list of (x1, y1, x2, y2). Two gates are conditional.
static bool drop_hits_frame(int x, int y)
{
    // Upper walls converging to central column
    if (segment_hits_drop(x, y, 20,  60, 155, 100)) return true;
    if (segment_hits_drop(x, y, SCREEN_W - 20, 60, 165, 100)) return true;
    // Upper wall gap: fill in the wall if gate is closed
    if (!s_gate_upper) {
        if (segment_hits_drop(x, y, GATE_UP_X1, GATE_UP_Y, GATE_UP_X2, GATE_UP_Y))
            return true;
    }
    // Central column
    if (x >= 155 && x <= 165 && y >= 100 && y <= 160) return true;
    // Lower walls fanning out
    if (segment_hits_drop(x, y, 50, 200, 155, 160)) return true;
    if (segment_hits_drop(x, y, SCREEN_W - 50, 200, 165, 160)) return true;
    // Lower gate is the middle of an open trough — only blocks if closed
    if (!s_gate_lower) {
        if (segment_hits_drop(x, y, GATE_LO_X1, GATE_LO_Y, GATE_LO_X2, GATE_LO_Y))
            return true;
    }
    // Catch cup (walls that funnel drops into the ground count area)
    if (x < 30 && y > 205) return true;
    if (x > SCREEN_W - 30 && y > 205) return true;
    return false;
}

// --- Rendering ---
static void draw_frame(void)
{
    // Convergent walls to central column.
    shrine_line(20,  60, 155, 100, C_LTRED);
    shrine_line(SCREEN_W - 20, 60, 165, 100, C_LTRED);
    // Central column
    shrine_fill_rect(155, 100, 11, 61, C_LTRED);
    // Lower fan walls
    shrine_line(50, 200, 155, 160, C_LTRED);
    shrine_line(SCREEN_W - 50, 200, 165, 160, C_LTRED);
    // Base catches
    shrine_fill_rect(0,  PLAY_BOT, 30, SCREEN_H - PLAY_BOT, C_LTRED);
    shrine_fill_rect(SCREEN_W - 30, PLAY_BOT, 30, SCREEN_H - PLAY_BOT, C_LTRED);
    // Gates — colored differently by open/closed state
    if (s_gate_upper)
        shrine_hline(GATE_UP_X1, GATE_UP_Y, GATE_UP_X2 - GATE_UP_X1, C_LTGREEN);
    else
        shrine_hline(GATE_UP_X1, GATE_UP_Y, GATE_UP_X2 - GATE_UP_X1, C_LTRED);
    if (s_gate_lower)
        shrine_hline(GATE_LO_X1, GATE_LO_Y, GATE_LO_X2 - GATE_LO_X1, C_LTGREEN);
    else
        shrine_hline(GATE_LO_X1, GATE_LO_Y, GATE_LO_X2 - GATE_LO_X1, C_LTRED);
}

static void draw_hud(void)
{
    char buf[24];
    shrine_fill_rect(0, 0, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(0, "*  RAINDROPS  *", C_YELLOW, C_BG);
    snprintf(buf, sizeof(buf), "CAUGHT %04d", s_caught);
    shrine_puts(1, 1, buf, C_LTCYAN, C_BG);
    shrine_puts(TEXT_COLS - 25, 1,
                s_gate_upper ? "A UPPER=OPEN" : "A UPPER=SHUT",
                s_gate_upper ? C_LTGREEN : C_LTRED, C_BG);
    shrine_puts(TEXT_COLS - 12, 1,
                s_gate_lower ? "B LOW=OPEN" : "B LOW=SHUT",
                s_gate_lower ? C_LTGREEN : C_LTRED, C_BG);
    shrine_puts_centered(TEXT_ROWS - 1, "BOOT EXIT", C_LTGRAY, C_BG);
}

// --- Physics ---
static void spawn_drop(int i)
{
    s_drops[i].x = (int)shrine_god(SCREEN_W - 20) + 10;
    s_drops[i].y = PLAY_TOP + (int)shrine_god(8);
    s_drops[i].vx = ((int)shrine_god(3)) - 1;   // -1..+1
    s_drops[i].vy = 1 + (int)shrine_god(2);     // 1..2
    s_drops[i].active = true;
}

static void update_drops(void)
{
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drops[i].active) {
            if (shrine_god(6) == 0) spawn_drop(i);
            continue;
        }
        int nx = s_drops[i].x + s_drops[i].vx;
        int ny = s_drops[i].y + s_drops[i].vy;
        if (nx < 2)             { nx = 2;             s_drops[i].vx = -s_drops[i].vx; }
        if (nx >= SCREEN_W - 2) { nx = SCREEN_W - 3;  s_drops[i].vx = -s_drops[i].vx; }
        if (ny >= PLAY_BOT) {
            s_drops[i].active = false;
            s_caught++;
            continue;
        }
        if (drop_hits_frame(nx, ny)) {
            // Slide sideways one pixel and retry once, mimicking Terry's
            // "flow around obstacles" behavior.
            int sx = nx + (s_drops[i].vx >= 0 ? +1 : -1);
            if (sx > 2 && sx < SCREEN_W - 2 && !drop_hits_frame(sx, ny)) {
                nx = sx;
                s_drops[i].vy = 1;
            } else {
                s_drops[i].active = false;
                continue;
            }
        }
        s_drops[i].x = nx;
        s_drops[i].y = ny;
    }
}

static void draw_drops(void)
{
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drops[i].active) continue;
        shrine_pixel(s_drops[i].x, s_drops[i].y, C_LTCYAN);
    }
}

// Only redraw the play area (below HUD, above bottom hint).
static void clear_play(void)
{
    shrine_fill_rect(0, PLAY_TOP, SCREEN_W, PLAY_BOT - PLAY_TOP + 1,
                     PAL_RGB565[C_BG]);
}

void game_raindrops_run(void)
{
    memset(s_drops, 0, sizeof(s_drops));
    s_caught = 0;
    s_gate_upper = true;
    s_gate_lower = false;

    shrine_clear(C_BG);
    draw_hud();

    uint32_t last_hud = shrine_ms();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (shrine_key_pressed(BTN_A)) {
            s_gate_upper = !s_gate_upper;
            draw_hud();
            shrine_beep(1600, 30);
        }
        if (shrine_key_pressed(BTN_B)) {
            s_gate_lower = !s_gate_lower;
            draw_hud();
            shrine_beep(1400, 30);
        }

        update_drops();

        clear_play();
        draw_frame();
        draw_drops();

        uint32_t now = shrine_ms();
        if (now - last_hud > 400) { draw_hud(); last_hud = now; }

        shrine_sleep_ms(30);
    }
}
