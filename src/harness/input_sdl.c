// input_sdl.c — SDL2 keyboard backend for input.h.
//
// Key mapping:
//   Arrow keys     LEFT / UP / DOWN / RIGHT
//   Z              A
//   X              B
//   Enter / Space  BOOT   (used both as "start" and as "exit-to-launcher")
//   Escape         Also BOOT, since it's the natural desktop key
//   Q              Quit the whole harness

#include "input.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

static uint8_t  s_prev, s_curr, s_edge_down, s_edge_up;
static uint32_t s_hold_start_ms[BTN_COUNT];

void input_init(void) { s_prev = s_curr = 0; }

static uint8_t read_keyboard(void)
{
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t r = 0;
    if (keys[SDL_SCANCODE_LEFT])                              r |= (1 << BTN_LEFT);
    if (keys[SDL_SCANCODE_UP])                                r |= (1 << BTN_UP);
    if (keys[SDL_SCANCODE_DOWN])                              r |= (1 << BTN_DOWN);
    if (keys[SDL_SCANCODE_RIGHT])                             r |= (1 << BTN_RIGHT);
    if (keys[SDL_SCANCODE_X])                                 r |= (1 << BTN_B);
    if (keys[SDL_SCANCODE_Z])                                 r |= (1 << BTN_A);
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]
        || keys[SDL_SCANCODE_ESCAPE])                         r |= (1 << BTN_BOOT);
    return r;
}

void input_scan(void)
{
    // Pump SDL events (window close, resize, etc.) and let Q quit the app.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) exit(0);
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_q) exit(0);
    }

    uint8_t raw = read_keyboard();
    uint8_t stable = (raw & s_prev) | (s_curr & ~(raw ^ s_prev));
    s_edge_down = stable & ~s_curr;
    s_edge_up   = ~stable &  s_curr;
    s_prev = raw;
    s_curr = stable;

    uint32_t now = SDL_GetTicks();
    for (int i = 0; i < BTN_COUNT; i++) {
        if (s_edge_down & (1 << i)) s_hold_start_ms[i] = now;
        if (!(s_curr    & (1 << i))) s_hold_start_ms[i] = 0;
    }
}

bool input_pressed (btn_t b) { return (s_edge_down & (1 << b)) != 0; }
bool input_held    (btn_t b) { return (s_curr      & (1 << b)) != 0; }
bool input_released(btn_t b) { return (s_edge_up   & (1 << b)) != 0; }

uint32_t input_hold_ms(btn_t b)
{
    if (!(s_curr & (1 << b))) return 0;
    return SDL_GetTicks() - s_hold_start_ms[b];
}

bool input_any_pressed(void) { return s_edge_down != 0; }
