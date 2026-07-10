// game_tictactoe.c — Terry's TicTacToe. Player is X, badge is O.
// Simple heuristic AI (win → block → center → corner → random).

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

#define CELL_PX  48
#define GRID_X   ((SCREEN_W - CELL_PX * 3) / 2)
#define GRID_Y   40

static uint8_t s_board[9];   // 0=empty, 1=X, 2=O
static int     s_cur;        // 0..8 cursor cell
static int     s_winner;     // 0=none, 1=X, 2=O, 3=draw

static const int WIN_LINES[8][3] = {
    {0,1,2},{3,4,5},{6,7,8},
    {0,3,6},{1,4,7},{2,5,8},
    {0,4,8},{2,4,6},
};

static int check_winner(void)
{
    for (int i = 0; i < 8; i++) {
        int a = s_board[WIN_LINES[i][0]];
        int b = s_board[WIN_LINES[i][1]];
        int c = s_board[WIN_LINES[i][2]];
        if (a && a == b && b == c) return a;
    }
    for (int i = 0; i < 9; i++) if (!s_board[i]) return 0;
    return 3;   // draw
}

static int find_winning_move(int player)
{
    for (int i = 0; i < 9; i++) {
        if (s_board[i]) continue;
        s_board[i] = player;
        int w = check_winner();
        s_board[i] = 0;
        if (w == player) return i;
    }
    return -1;
}

static int ai_pick(void)
{
    int m;
    if ((m = find_winning_move(2)) >= 0) return m;   // win
    if ((m = find_winning_move(1)) >= 0) return m;   // block
    if (!s_board[4]) return 4;                       // center
    const int corners[] = {0,2,6,8};
    for (int i = 0; i < 4; i++) if (!s_board[corners[i]]) return corners[i];
    for (int i = 0; i < 9; i++) if (!s_board[i]) return i;
    return -1;
}

static void draw_mark(int cell, int mark, color_t c)
{
    int cx = GRID_X + (cell % 3) * CELL_PX + CELL_PX / 2;
    int cy = GRID_Y + (cell / 3) * CELL_PX + CELL_PX / 2;
    if (mark == 1) {
        // X — two diagonal lines
        for (int i = -14; i <= 14; i++) {
            shrine_pixel(cx + i,     cy + i,     c);
            shrine_pixel(cx + i,     cy + i + 1, c);
            shrine_pixel(cx + i,     cy - i,     c);
            shrine_pixel(cx + i,     cy - i + 1, c);
        }
    } else if (mark == 2) {
        // O
        shrine_circle(cx, cy, 15, c);
        shrine_circle(cx, cy, 14, c);
    }
}

static void draw_grid(void)
{
    // Grid lines
    for (int i = 1; i < 3; i++) {
        shrine_fill_rect(GRID_X + i * CELL_PX - 1, GRID_Y,
                         2, CELL_PX * 3, C_LTCYAN);
        shrine_fill_rect(GRID_X, GRID_Y + i * CELL_PX - 1,
                         CELL_PX * 3, 2, C_LTCYAN);
    }
}

static void draw_cell(int i)
{
    int cx = GRID_X + (i % 3) * CELL_PX;
    int cy = GRID_Y + (i / 3) * CELL_PX;
    // Clear (leaving 1px around for grid lines).
    shrine_fill_rect(cx + 2, cy + 2, CELL_PX - 4, CELL_PX - 4, C_BG);
    // Highlight cursor.
    if (i == s_cur && !s_winner) {
        shrine_rect(cx + 4, cy + 4, CELL_PX - 8, CELL_PX - 8, C_YELLOW);
        shrine_rect(cx + 5, cy + 5, CELL_PX - 10, CELL_PX - 10, C_YELLOW);
    }
    if (s_board[i] == 1) draw_mark(i, 1, C_LTGREEN);
    if (s_board[i] == 2) draw_mark(i, 2, C_LTRED);
}

static void draw_all_cells(void)
{
    for (int i = 0; i < 9; i++) draw_cell(i);
}

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  TIC TAC TOE  *", C_YELLOW, C_BG);
    shrine_puts_centered(3, "X = THEE  //  O = FALLEN", C_LTCYAN, C_BG);
    shrine_puts_centered(27, "ARROWS: MOVE   A: PLACE   BOOT: EXIT",
                         C_LTGRAY, C_BG);
}

static void reset_game(void)
{
    memset(s_board, 0, sizeof(s_board));
    s_cur = 4;
    s_winner = 0;
}

static void draw_status(const char *msg, color_t c)
{
    // Clear status row.
    shrine_fill_rect(0, 24 * GLYPH_H, SCREEN_W, GLYPH_H, PAL_RGB565[C_BG]);
    shrine_puts_centered(24, msg, c, C_BG);
}

void game_tictactoe_run(void)
{
    reset_game();
    draw_chrome();
    draw_grid();
    draw_all_cells();
    draw_status("MAKE THY MOVE.", C_WHITE);

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (!s_winner) {
            int old_cur = s_cur;
            if (shrine_key_pressed(BTN_LEFT))  s_cur = (s_cur - 1 + 9) % 9;
            if (shrine_key_pressed(BTN_RIGHT)) s_cur = (s_cur + 1) % 9;
            if (shrine_key_pressed(BTN_UP))    s_cur = (s_cur - 3 + 9) % 9;
            if (shrine_key_pressed(BTN_DOWN))  s_cur = (s_cur + 3) % 9;
            if (old_cur != s_cur) {
                draw_cell(old_cur);
                draw_cell(s_cur);
                shrine_beep(2400, 15);
            }
            if (shrine_key_pressed(BTN_A) && !s_board[s_cur]) {
                s_board[s_cur] = 1;
                draw_cell(s_cur);
                shrine_beep(1400, 40);
                s_winner = check_winner();
                if (!s_winner) {
                    // AI plays.
                    shrine_sleep_ms(300);
                    int m = ai_pick();
                    if (m >= 0) {
                        s_board[m] = 2;
                        draw_cell(m);
                        shrine_beep(600, 60);
                    }
                    s_winner = check_winner();
                }
                if (s_winner == 1) draw_status("* THY VICTORY *", C_LTGREEN);
                else if (s_winner == 2) draw_status("* THE FALLEN PREVAIL *", C_LTRED);
                else if (s_winner == 3) draw_status("* A DRAW *", C_YELLOW);
            }
        } else {
            // After game — any key restarts.
            if (shrine_any_pressed()) {
                reset_game();
                draw_grid();
                draw_all_cells();
                draw_status("MAKE THY MOVE.", C_WHITE);
            }
        }

        shrine_sleep_ms(20);
    }
}
