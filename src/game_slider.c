// game_slider.c — 15-puzzle. Slide tiles to arrange 1-15 in order.
// D-pad = direction to slide the empty space.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

#define N       4
#define TILE_PX 44
#define GRID_X  ((SCREEN_W - TILE_PX * N) / 2)
#define GRID_Y  32

static uint8_t s_board[N * N];   // 0 = empty
static int     s_hole;           // index of empty cell
static int     s_moves;
static bool    s_solved;

static void draw_tile(int idx)
{
    int r = idx / N, c = idx % N;
    int x = GRID_X + c * TILE_PX;
    int y = GRID_Y + r * TILE_PX;

    if (s_board[idx] == 0) {
        // Empty — dark blue interior with a subtle shade pattern.
        shrine_fill_rect(x + 1, y + 1, TILE_PX - 2, TILE_PX - 2, C_BLACK);
        shrine_rect(x, y, TILE_PX, TILE_PX, C_DKGRAY);
        return;
    }
    // Tile — light cyan face, blue border, yellow number.
    shrine_fill_rect(x + 1, y + 1, TILE_PX - 2, TILE_PX - 2, C_LTCYAN);
    shrine_rect(x, y, TILE_PX, TILE_PX, C_YELLOW);
    // Inner highlight.
    shrine_hline(x + 2, y + 2, TILE_PX - 4, C_WHITE);
    shrine_vline(x + 2, y + 2, TILE_PX - 4, C_WHITE);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", s_board[idx]);
    int len = (int)strlen(buf);
    int scale = 3;
    int text_w = 8 * scale * len;
    int text_h = 8 * scale;
    int tx = x + (TILE_PX - text_w) / 2;
    int ty = y + (TILE_PX - text_h) / 2;
    shrine_putsxy_scaled(tx, ty, buf, C_BLUE, C_LTCYAN, scale);
}

static void redraw_board(void)
{
    for (int i = 0; i < N * N; i++) draw_tile(i);
}

static bool check_solved(void)
{
    for (int i = 0; i < N * N - 1; i++) if (s_board[i] != i + 1) return false;
    return s_board[N * N - 1] == 0;
}

static void try_move(int dir)
{
    // dir: 0=UP (hole moves up), 1=DOWN, 2=LEFT, 3=RIGHT.
    int hr = s_hole / N, hc = s_hole % N;
    int nr = hr, nc = hc;
    if (dir == 0) nr--;
    if (dir == 1) nr++;
    if (dir == 2) nc--;
    if (dir == 3) nc++;
    if (nr < 0 || nr >= N || nc < 0 || nc >= N) return;
    int ni = nr * N + nc;
    // Swap hole with neighbor tile.
    s_board[s_hole] = s_board[ni];
    s_board[ni] = 0;
    int old_hole = s_hole;
    s_hole = ni;
    draw_tile(old_hole);
    draw_tile(s_hole);
    s_moves++;
    shrine_beep(1400, 20);
}

static void shuffle(int steps)
{
    for (int i = 0; i < steps; i++) {
        int dirs[4];
        int nd = 0;
        int hr = s_hole / N, hc = s_hole % N;
        if (hr > 0)     dirs[nd++] = 0;
        if (hr < N - 1) dirs[nd++] = 1;
        if (hc > 0)     dirs[nd++] = 2;
        if (hc < N - 1) dirs[nd++] = 3;
        int dir = dirs[shrine_god(nd)];
        int nr = hr, nc = hc;
        if (dir == 0) nr--;
        if (dir == 1) nr++;
        if (dir == 2) nc--;
        if (dir == 3) nc++;
        int ni = nr * N + nc;
        s_board[s_hole] = s_board[ni];
        s_board[ni] = 0;
        s_hole = ni;
    }
}

static void reset(void)
{
    for (int i = 0; i < N * N - 1; i++) s_board[i] = i + 1;
    s_board[N * N - 1] = 0;
    s_hole = N * N - 1;
    shuffle(200);
    s_moves = 0;
    s_solved = false;
}

static void draw_status(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "MOVES: %d", s_moves);
    shrine_fill_rect(0, 27 * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(27, buf, C_WHITE, C_BG);
}

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  SLIDER  *", C_YELLOW, C_BG);
    shrine_puts_centered(28, "ARROWS: SLIDE   BOOT: EXIT",
                         C_LTGRAY, C_BG);
}

void game_slider_run(void)
{
    reset();
    draw_chrome();
    redraw_board();
    draw_status();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (!s_solved) {
            if (shrine_key_pressed(BTN_UP))    { try_move(0); draw_status(); s_solved = check_solved(); }
            if (shrine_key_pressed(BTN_DOWN))  { try_move(1); draw_status(); s_solved = check_solved(); }
            if (shrine_key_pressed(BTN_LEFT))  { try_move(2); draw_status(); s_solved = check_solved(); }
            if (shrine_key_pressed(BTN_RIGHT)) { try_move(3); draw_status(); s_solved = check_solved(); }
            if (s_solved) {
                shrine_puts_centered(26, "* HALLELUJAH *", C_YELLOW, C_BG);
                shrine_beep(1600, 100);
                shrine_beep(2000, 100);
                shrine_beep(2400, 200);
            }
        } else {
            if (shrine_any_pressed()) {
                shrine_fill_rect(0, 26 * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
                reset();
                redraw_board();
                draw_status();
            }
        }

        shrine_sleep_ms(20);
    }
}
