// input.h — 7-button reader with debounce, edge detection, and hold time.

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BTN_LEFT = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_RIGHT,
    BTN_B,
    BTN_A,
    BTN_BOOT,
    BTN_COUNT,
} btn_t;

void input_init(void);
void input_scan(void);

bool input_pressed(btn_t b);
bool input_held(btn_t b);
bool input_released(btn_t b);

uint32_t input_hold_ms(btn_t b);

// True if ANY button is currently pressed (edge, not held) — used for
// "press any key to continue" style prompts.
bool input_any_pressed(void);
