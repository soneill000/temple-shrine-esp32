// games.h — collective header for every game/app entry point.
// Each game runs until the user issues the quit gesture (hold BOOT), then
// returns. The launcher calls one at a time.

#pragma once

void game_oracle_run(void);
void game_hymn_run(void);
void game_chronicle_run(void);
void game_tictactoe_run(void);
void game_slider_run(void);
void game_flapbat_run(void);
void game_whap_run(void);

// Real ports from Terry's TempleOS Demo/Games/*.HC.
void game_digits_run(void);
void game_bombergolf_run(void);
