// game_flapbat.c — FlapBat: press A to flap. Gravity pulls you down.
// Fly through temple pillars. TempleOS bat. Very unforgiving.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

#define PLAY_TOP    24
#define PLAY_BOT    216
#define PLAY_H_PX   (PLAY_BOT - PLAY_TOP)
#define BAT_X       60
#define BAT_SIZE    16
#define WALL_W      28
#define GAP_H       74
#define N_WALLS     3

typedef struct { int x; int gap_y; bool scored; } wall_t;

static int     s_bat_y;      // integer y (pixels)
static int     s_bat_vy;     // ×10 to preserve fractional velocity
static wall_t  s_walls[N_WALLS];
static int     s_score;
static bool    s_dead;

static void spawn_wall(int i, int start_x)
{
    s_walls[i].x = start_x;
    s_walls[i].gap_y = PLAY_TOP + 20 + (int)shrine_god(PLAY_H_PX - GAP_H - 40);
    s_walls[i].scored = false;
}

static void reset(void)
{
    s_bat_y = PLAY_TOP + PLAY_H_PX / 2;
    s_bat_vy = 0;
    for (int i = 0; i < N_WALLS; i++)
        spawn_wall(i, SCREEN_W + 40 + i * 110);
    s_score = 0;
    s_dead = false;
}

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  FLAPBAT  *", C_YELLOW, C_BG);
    // Top boundary
    shrine_fill_rect(0, PLAY_TOP - 2, SCREEN_W, 2, C_YELLOW);
    // Bottom boundary (ground)
    shrine_fill_rect(0, PLAY_BOT, SCREEN_W, 2, C_YELLOW);
    shrine_fill_rect(0, PLAY_BOT + 2, SCREEN_W, 4, C_BROWN);
    shrine_puts_centered(28, "A: FLAP   BOOT: EXIT", C_LTGRAY, C_BG);
}

static void draw_bat(int y, color_t c)
{
    // 16x16 bat: scaled 8x8 bat glyph, drawn from font G_BAT.
    const uint8_t *g = FONT8X8[(uint8_t)G_BAT[0]];
    for (int r = 0; r < 8; r++) {
        for (int col = 0; col < 8; col++) {
            if (g[r] & (1 << col)) {
                shrine_fill_rect(BAT_X + col * 2, y + r * 2, 2, 2, c);
            }
        }
    }
}

static void erase_bat(int y)
{
    // Erase the whole 16x16 bounding box with background.
    shrine_fill_rect(BAT_X, y, BAT_SIZE, BAT_SIZE, C_BG);
}

static void draw_wall(const wall_t *w)
{
    if (w->x < -WALL_W || w->x > SCREEN_W) return;
    // Top pillar.
    if (w->gap_y > PLAY_TOP) {
        shrine_fill_rect(w->x, PLAY_TOP, WALL_W, w->gap_y - PLAY_TOP, C_LTCYAN);
        shrine_rect(w->x, PLAY_TOP, WALL_W, w->gap_y - PLAY_TOP, C_YELLOW);
    }
    // Bottom pillar.
    int bot_y = w->gap_y + GAP_H;
    if (bot_y < PLAY_BOT) {
        shrine_fill_rect(w->x, bot_y, WALL_W, PLAY_BOT - bot_y, C_LTCYAN);
        shrine_rect(w->x, bot_y, WALL_W, PLAY_BOT - bot_y, C_YELLOW);
    }
}

static void erase_wall_column(int x)
{
    // Clear the vertical strip vacated by a wall moving left.
    shrine_fill_rect(x, PLAY_TOP, WALL_W, PLAY_H_PX, C_BG);
}

static bool bat_hits_wall(int y, const wall_t *w)
{
    if (w->x + WALL_W < BAT_X)       return false;
    if (w->x > BAT_X + BAT_SIZE - 1) return false;
    if (y >= w->gap_y && y + BAT_SIZE - 1 < w->gap_y + GAP_H) return false;
    return true;
}

static void draw_score(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "SCORE %d", s_score);
    shrine_fill_rect(0, 26 * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(26, buf, C_LTGREEN, C_BG);
}

void game_flapbat_run(void)
{
    reset();
    draw_chrome();
    for (int i = 0; i < N_WALLS; i++) draw_wall(&s_walls[i]);
    draw_bat(s_bat_y, C_LTMAGENTA);
    draw_score();

    // Waiting-to-start state — first tap starts play.
    shrine_puts_centered(15, "PRESS A TO BEGIN", C_YELLOW, C_BG);
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;
        if (shrine_key_pressed(BTN_A)) {
            shrine_fill_rect(0, 15 * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
            s_bat_vy = -32;   // initial hop
            shrine_beep(1200, 30);
            break;
        }
        shrine_sleep_ms(30);
    }

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (!s_dead) {
            if (shrine_key_pressed(BTN_A)) {
                s_bat_vy = -32;
                shrine_beep(1600, 25);
            }
            // Physics (integers x10 for fractional velocity).
            s_bat_vy += 3;              // gravity per tick
            if (s_bat_vy > 55) s_bat_vy = 55;
            int old_y = s_bat_y;
            s_bat_y += s_bat_vy / 10;

            // Wall scroll — 3 px/frame.
            for (int i = 0; i < N_WALLS; i++) {
                int old_x = s_walls[i].x;
                s_walls[i].x -= 3;
                // Erase vacated column on the right side of the wall.
                if (old_x > s_walls[i].x)
                    erase_wall_column(old_x + WALL_W - 3);
                if (s_walls[i].x + WALL_W < 0) {
                    // recycle to the right of the rightmost wall
                    int max_x = 0;
                    for (int j = 0; j < N_WALLS; j++)
                        if (s_walls[j].x > max_x) max_x = s_walls[j].x;
                    spawn_wall(i, max_x + 110);
                }
                draw_wall(&s_walls[i]);

                if (!s_walls[i].scored && s_walls[i].x + WALL_W < BAT_X) {
                    s_walls[i].scored = true;
                    s_score++;
                    shrine_beep(2000, 40);
                    draw_score();
                }

                if (bat_hits_wall(s_bat_y, &s_walls[i])) s_dead = true;
            }

            if (s_bat_y < PLAY_TOP || s_bat_y + BAT_SIZE > PLAY_BOT) s_dead = true;

            // Redraw bat.
            if (old_y != s_bat_y) erase_bat(old_y);
            draw_bat(s_bat_y, C_LTMAGENTA);

            if (s_dead) {
                shrine_beep(300, 200);
                shrine_beep(200, 300);
                shrine_puts_centered(13, "* THE BAT HATH FALLEN *",
                                     C_LTRED, C_BG);
                shrine_puts_centered(15, "PRESS A TO RISE AGAIN",
                                     C_YELLOW, C_BG);
            }
        } else {
            if (shrine_key_pressed(BTN_A)) {
                reset();
                draw_chrome();
                for (int i = 0; i < N_WALLS; i++) draw_wall(&s_walls[i]);
                draw_bat(s_bat_y, C_LTMAGENTA);
                draw_score();
                s_bat_vy = -32;
                shrine_beep(1200, 30);
            }
        }

        shrine_sleep_ms(50);   // ~20 fps
    }
}
