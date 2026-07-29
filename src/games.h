// games.h — collective header for every game/app entry point.
// Each game runs until the user issues the quit gesture (hold BOOT), then
// returns. The launcher calls one at a time.

#pragma once

void game_oracle_run(void);
void game_hymn_run(void);
void game_chronicle_run(void);
// (game_tictactoe removed — not enough Terry-specific charm)
// (game_slider removed — same reason)
void game_bugbird_run(void);   // Terry's actual bird-catches-bugs (replaces FlapBat)
void game_whap_run(void);
void game_talons_run(void);   // Tier-2 voxel-terrain flight demo

// Real ports from Terry's TempleOS Demo/Games/*.HC.
void game_digits_run(void);
void game_bombergolf_run(void);
void game_squirt_run(void);
void game_raindrops_run(void);

// Terry's flagship — Moses journeys through the wilderness.
void game_afteregypt_run(void);

// KJV verse browser in TempleOS DolDoc blue/white style.
void game_bible_run(void);

// (Varoom removed — Terry's cars are SPT_MESH sprites we don't render;
//  low ROI to add mesh support just for this one game.)

// HOLYMESH — LoRa broadcaster + receiver for Terry-themed aphorisms
// and GodWords over Meshtastic-compatible radio (was PILLAR OF CLOUD).
void game_lora_run(void);

// HOLYC — Terry-style REPL cameo. Curated command palette on a yellow/
// blue TempleOS terminal with CRT scanlines + monitor-flash effect.
void game_holyc_run(void);
