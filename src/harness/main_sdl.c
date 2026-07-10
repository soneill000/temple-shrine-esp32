// main_sdl.c — desktop entry point for TempleShrine.
// Same flow as the ESP main.c: init, boot chime (stubbed to stderr on
// desktop), then loop splash + menu forever.

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "shrine.h"
#include "splash.h"
#include "menu.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    atexit(SDL_Quit);

    // Seed the C rand() we use in place of esp_random().
    srand((unsigned)time(NULL));

    printf("TempleShrine (desktop harness) starting...\n");
    printf("Keys: arrows = d-pad, Z = A, X = B, Enter/Space/Esc = BOOT, "
           "Q = quit harness\n");

    shrine_init();

    // Boot chime — silent on the harness, but keep the delays so timing
    // roughly matches the badge experience.
    shrine_beep(392, 120);
    shrine_beep(523, 120);
    shrine_beep(784, 200);

    while (1) {
        splash_run();
        menu_run();
    }
    return 0;
}
