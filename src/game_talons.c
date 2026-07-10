// game_talons.c — Talons: hawks dive from heaven. Fire arrows to smite them.
// D-pad LEFT/RIGHT to move, A to fire. Brutal by design.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

#define PLAY_TOP      24
#define PLAY_BOT      216
#define PLAYER_Y      (PLAY_BOT - 18)
#define PLAYER_W      16
#define MAX_BULLETS   6
#define MAX_HAWKS     6

typedef struct { int x, y; bool active; } bullet_t;
typedef struct { int x, y, vy; bool active; } hawk_t;

static const uint8_t HAWK[8] = {
    0x00,
    0x66,   // .XX..XX.
    0xFF,   // XXXXXXXX
    0xFF,   // XXXXXXXX
    0x7E,   // .XXXXXX.
    0x3C,   // ..XXXX..
    0x18,   // ...XX...
    0x00,
};

static int      s_px;
static bullet_t s_bullets[MAX_BULLETS];
static hawk_t   s_hawks[MAX_HAWKS];
static int      s_score;
static int      s_lives;
static uint32_t s_next_spawn;
static bool     s_dead;

static void draw_player(int x, color_t c)
{
    // Small archer: a filled diamond.
    shrine_fill_rect(x + 6, PLAYER_Y,     4, 2, c);
    shrine_fill_rect(x + 4, PLAYER_Y + 2, 8, 2, c);
    shrine_fill_rect(x + 2, PLAYER_Y + 4, 12, 4, c);
    shrine_fill_rect(x + 4, PLAYER_Y + 8, 8, 2, c);
    shrine_fill_rect(x + 6, PLAYER_Y + 10, 4, 4, c);
}

static void erase_player(int x)
{
    shrine_fill_rect(x, PLAYER_Y, PLAYER_W, 16, C_BG);
}

static void draw_bullet(int x, int y, color_t c)
{
    shrine_fill_rect(x, y, 2, 4, c);
}

static void draw_hawk(int x, int y, color_t c)
{
    const uint8_t *g = HAWK;
    for (int r = 0; r < 8; r++) {
        for (int col = 0; col < 8; col++) {
            if (g[r] & (1 << col))
                shrine_fill_rect(x + col * 2, y + r * 2, 2, 2, c);
        }
    }
}

static void erase_hawk(int x, int y)
{
    shrine_fill_rect(x, y, 16, 16, C_BG);
}

static void fire_bullet(void)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!s_bullets[i].active) {
            s_bullets[i].x = s_px + PLAYER_W / 2 - 1;
            s_bullets[i].y = PLAYER_Y - 4;
            s_bullets[i].active = true;
            shrine_beep(2400, 20);
            return;
        }
    }
}

static void spawn_hawk(void)
{
    for (int i = 0; i < MAX_HAWKS; i++) {
        if (!s_hawks[i].active) {
            s_hawks[i].x = (int)shrine_god(SCREEN_W - 20) + 2;
            s_hawks[i].y = PLAY_TOP;
            s_hawks[i].vy = 1 + (int)shrine_god(3) + s_score / 5;
            if (s_hawks[i].vy > 6) s_hawks[i].vy = 6;
            s_hawks[i].active = true;
            return;
        }
    }
}

static bool collides(int bx, int by, int hx, int hy)
{
    if (bx + 2 < hx || bx > hx + 15) return false;
    if (by + 4 < hy || by > hy + 15) return false;
    return true;
}

static void draw_hud(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "KILLS %d   LIVES %d", s_score, s_lives);
    shrine_fill_rect(0, 26 * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(26, buf, C_WHITE, C_BG);
}

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  TALONS  *", C_YELLOW, C_BG);
    shrine_puts_centered(3, "HAWKS DIVE. STAND FIRM.", C_LTCYAN, C_BG);
    shrine_fill_rect(0, PLAY_TOP - 2, SCREEN_W, 2, C_YELLOW);
    shrine_fill_rect(0, PLAY_BOT,     SCREEN_W, 2, C_YELLOW);
    shrine_puts_centered(28, "L/R MOVE   A FIRE   BOOT EXIT",
                         C_LTGRAY, C_BG);
}

static void reset(void)
{
    s_px = SCREEN_W / 2 - PLAYER_W / 2;
    memset(s_bullets, 0, sizeof(s_bullets));
    memset(s_hawks,   0, sizeof(s_hawks));
    s_score = 0;
    s_lives = 3;
    s_next_spawn = shrine_ms() + 1000;
    s_dead = false;
}

void game_talons_run(void)
{
    reset();
    draw_chrome();
    draw_player(s_px, C_LTGREEN);
    draw_hud();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (!s_dead) {
            // Player movement.
            int old_px = s_px;
            if (shrine_key_held(BTN_LEFT))  s_px -= 3;
            if (shrine_key_held(BTN_RIGHT)) s_px += 3;
            if (s_px < 0) s_px = 0;
            if (s_px > SCREEN_W - PLAYER_W) s_px = SCREEN_W - PLAYER_W;
            if (old_px != s_px) {
                erase_player(old_px);
                draw_player(s_px, C_LTGREEN);
            }
            // Fire.
            if (shrine_key_pressed(BTN_A)) fire_bullet();

            // Bullets.
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!s_bullets[i].active) continue;
                int old_y = s_bullets[i].y;
                s_bullets[i].y -= 6;
                shrine_fill_rect(s_bullets[i].x, old_y, 2, 4, C_BG);
                if (s_bullets[i].y < PLAY_TOP) { s_bullets[i].active = false; continue; }
                draw_bullet(s_bullets[i].x, s_bullets[i].y, C_YELLOW);
            }

            // Hawks.
            for (int i = 0; i < MAX_HAWKS; i++) {
                if (!s_hawks[i].active) continue;
                int old_y = s_hawks[i].y;
                s_hawks[i].y += s_hawks[i].vy;
                erase_hawk(s_hawks[i].x, old_y);
                if (s_hawks[i].y + 16 >= PLAY_BOT) {
                    s_hawks[i].active = false;
                    s_lives--;
                    shrine_beep(200, 200);
                    draw_hud();
                    if (s_lives <= 0) { s_dead = true; }
                    continue;
                }
                draw_hawk(s_hawks[i].x, s_hawks[i].y, C_LTRED);
            }

            // Collisions.
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!s_bullets[i].active) continue;
                for (int j = 0; j < MAX_HAWKS; j++) {
                    if (!s_hawks[j].active) continue;
                    if (collides(s_bullets[i].x, s_bullets[i].y,
                                 s_hawks[j].x, s_hawks[j].y)) {
                        shrine_fill_rect(s_bullets[i].x, s_bullets[i].y,
                                         2, 4, C_BG);
                        erase_hawk(s_hawks[j].x, s_hawks[j].y);
                        s_bullets[i].active = false;
                        s_hawks[j].active = false;
                        s_score++;
                        shrine_beep(2600, 40);
                        draw_hud();
                        break;
                    }
                }
            }

            // Spawn.
            uint32_t now = shrine_ms();
            if (now >= s_next_spawn) {
                spawn_hawk();
                int interval = 1200 - s_score * 40;
                if (interval < 350) interval = 350;
                s_next_spawn = now + interval;
            }

            if (s_dead) {
                shrine_puts_centered(13, "* THOU ART SLAIN *",
                                     C_LTRED, C_BG);
                shrine_puts_centered(15, "PRESS A TO RISE AGAIN",
                                     C_WHITE, C_BG);
            }
        } else {
            if (shrine_key_pressed(BTN_A)) {
                draw_chrome();
                reset();
                draw_player(s_px, C_LTGREEN);
                draw_hud();
            }
        }

        shrine_sleep_ms(40);
    }
}
