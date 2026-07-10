// game_whap.c — Whack-a-mole with fallen angels rising from pits.
// 3x3 grid of pits. A demon appears, you have limited time to smite it.
// 60-second round. Score = smitings. Miss = pit closes empty.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

#define PIT_PX     50
#define GRID_COLS  3
#define GRID_ROWS  3
#define GRID_X     ((SCREEN_W - PIT_PX * GRID_COLS) / 2)
#define GRID_Y     40
#define ROUND_MS   60000

// Demon sprite (8x8). Two horns, angry eyes, mouth.
static const uint8_t DEMON[8] = {
    0x81,   // X......X
    0xC3,   // XX....XX
    0x66,   // .XX..XX.
    0x3C,   // ..XXXX..
    0x5A,   // .X.XX.X.
    0x7E,   // .XXXXXX.
    0x24,   // ..X..X..
    0x18,   // ...XX...
};

// Halo (Terry loved his hearts and halos on angels).
static const uint8_t ANGEL[8] = {
    0x18,
    0x3C,
    0x66,
    0xC3,
    0xC3,
    0x66,
    0x3C,
    0x18,
};

static int  s_cur;            // cursor pit 0..8
static int  s_active;         // -1 or pit index
static int  s_active_kind;    // 0 demon, 1 angel
static uint32_t s_active_until;
static uint32_t s_next_spawn;
static uint32_t s_round_end;
static int  s_score;
static int  s_missed;
static bool s_round_over;

static void pit_coords(int idx, int *x, int *y)
{
    *x = GRID_X + (idx % GRID_COLS) * PIT_PX;
    *y = GRID_Y + (idx / GRID_COLS) * PIT_PX;
}

static void draw_pit(int idx)
{
    int x, y;
    pit_coords(idx, &x, &y);
    // Dark hole background.
    shrine_fill_rect(x + 4, y + 4, PIT_PX - 8, PIT_PX - 8, C_BLACK);
    // Stone rim.
    shrine_rect(x + 2, y + 2, PIT_PX - 4, PIT_PX - 4, C_LTGRAY);
    shrine_rect(x + 3, y + 3, PIT_PX - 6, PIT_PX - 6, C_LTGRAY);
    // Cursor highlight.
    if (idx == s_cur) {
        shrine_rect(x + 5, y + 5, PIT_PX - 10, PIT_PX - 10, C_YELLOW);
    }
    // Sprite in the middle if active.
    if (idx == s_active) {
        const uint8_t *g = (s_active_kind == 0) ? DEMON : ANGEL;
        color_t c = (s_active_kind == 0) ? C_LTRED : C_YELLOW;
        int cx = x + PIT_PX / 2 - 12;   // 24-wide sprite (8*3)
        int cy = y + PIT_PX / 2 - 12;
        for (int r = 0; r < 8; r++) {
            for (int col = 0; col < 8; col++) {
                if (g[r] & (1 << col))
                    shrine_fill_rect(cx + col * 3, cy + r * 3, 3, 3, c);
            }
        }
    }
}

static void schedule_next_spawn(void)
{
    uint32_t now = shrine_ms();
    // Faster as time runs out.
    uint32_t elapsed = ROUND_MS - (s_round_end - now);
    uint32_t interval = 900 - (elapsed / 100);
    if (interval < 350) interval = 350;
    s_next_spawn = now + interval;
}

static void spawn_target(void)
{
    // Pick an empty pit (i.e. not currently the active one).
    int pit = (int)shrine_god(GRID_COLS * GRID_ROWS);
    int old = s_active;
    s_active = pit;
    // 1-in-6 chance it's an angel (do not smite!).
    s_active_kind = (shrine_god(6) == 0) ? 1 : 0;
    uint32_t now = shrine_ms();
    uint32_t elapsed = ROUND_MS - (s_round_end - now);
    uint32_t life = 1400 - (elapsed / 60);
    if (life < 500) life = 500;
    s_active_until = now + life;
    if (old >= 0 && old != s_active) draw_pit(old);
    draw_pit(s_active);
}

static void draw_hud(void)
{
    char buf[32];
    uint32_t now = shrine_ms();
    int secs = 0;
    if (s_round_end > now) secs = (s_round_end - now) / 1000;
    snprintf(buf, sizeof(buf), "SMOTE %d   MISSED %d   T-%02d",
             s_score, s_missed, secs);
    shrine_fill_rect(0, 25 * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(25, buf, C_WHITE, C_BG);
}

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  WHAP  *", C_YELLOW, C_BG);
    shrine_puts_centered(3, "SMITE DEMONS. SPARE ANGELS.", C_LTCYAN, C_BG);
    shrine_puts_centered(28, "ARROWS: AIM   A: SMITE   BOOT: EXIT",
                         C_LTGRAY, C_BG);
}

static void reset_round(void)
{
    s_cur = 4;
    s_active = -1;
    s_score = 0;
    s_missed = 0;
    s_round_end = shrine_ms() + ROUND_MS;
    s_next_spawn = shrine_ms() + 800;
    s_round_over = false;
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) draw_pit(i);
    draw_hud();
}

void game_whap_run(void)
{
    draw_chrome();
    reset_round();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        uint32_t now = shrine_ms();

        if (!s_round_over) {
            // Cursor movement.
            int old_cur = s_cur;
            if (shrine_key_pressed(BTN_LEFT))  s_cur = (s_cur % GRID_COLS == 0) ? s_cur : s_cur - 1;
            if (shrine_key_pressed(BTN_RIGHT)) s_cur = (s_cur % GRID_COLS == GRID_COLS - 1) ? s_cur : s_cur + 1;
            if (shrine_key_pressed(BTN_UP))    s_cur = (s_cur / GRID_COLS == 0) ? s_cur : s_cur - GRID_COLS;
            if (shrine_key_pressed(BTN_DOWN))  s_cur = (s_cur / GRID_COLS == GRID_ROWS - 1) ? s_cur : s_cur + GRID_COLS;
            if (old_cur != s_cur) { draw_pit(old_cur); draw_pit(s_cur); shrine_beep(1800, 15); }

            // Strike.
            if (shrine_key_pressed(BTN_A)) {
                if (s_cur == s_active) {
                    if (s_active_kind == 0) {
                        s_score++;
                        shrine_beep(2000, 60);
                        shrine_beep(2600, 40);
                    } else {
                        // Struck an angel!
                        s_missed += 2;
                        shrine_beep(200, 200);
                    }
                    int old = s_active;
                    s_active = -1;
                    draw_pit(old);
                    draw_pit(s_cur);
                    draw_hud();
                    schedule_next_spawn();
                } else {
                    shrine_beep(400, 30);
                }
            }

            // Auto-timeout the current target.
            if (s_active >= 0 && now >= s_active_until) {
                if (s_active_kind == 0) s_missed++;
                int old = s_active;
                s_active = -1;
                draw_pit(old);
                draw_hud();
                schedule_next_spawn();
            }

            // Spawn a new target.
            if (s_active < 0 && now >= s_next_spawn) {
                spawn_target();
                draw_hud();
            }

            // Timer tick (once per second, update HUD).
            static uint32_t last_hud = 0;
            if (now - last_hud >= 500) { last_hud = now; draw_hud(); }

            if (now >= s_round_end) {
                s_round_over = true;
                if (s_active >= 0) { int old = s_active; s_active = -1; draw_pit(old); }
                shrine_puts_centered(13, "* THE ROUND IS ENDED *",
                                     C_YELLOW, C_BG);
                shrine_puts_centered(15, "PRESS A TO PLAY AGAIN",
                                     C_WHITE, C_BG);
                shrine_beep(2000, 100);
                shrine_beep(1600, 100);
                shrine_beep(1200, 200);
            }
        } else {
            if (shrine_key_pressed(BTN_A)) {
                draw_chrome();
                reset_round();
            }
        }

        shrine_sleep_ms(25);
    }
}
