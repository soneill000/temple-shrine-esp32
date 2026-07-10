// input.c — poll 7 active-low buttons, debounce, expose edge events.

#include "input.h"
#include "hw.h"

#include "driver/gpio.h"
#include "esp_timer.h"

static const gpio_num_t PINS[BTN_COUNT] = {
    [BTN_LEFT]  = PIN_BTN_LEFT,
    [BTN_UP]    = PIN_BTN_UP,
    [BTN_DOWN]  = PIN_BTN_DOWN,
    [BTN_RIGHT] = PIN_BTN_RIGHT,
    [BTN_B]     = PIN_BTN_B,
    [BTN_A]     = PIN_BTN_A,
    [BTN_BOOT]  = PIN_BTN_BOOT,
};

static uint8_t  s_prev, s_curr, s_edge_down, s_edge_up;
static uint32_t s_hold_start_ms[BTN_COUNT];

void input_init(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_reset_pin(PINS[i]);
        gpio_set_direction(PINS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(PINS[i], GPIO_PULLUP_ONLY);
    }
}

void input_scan(void)
{
    uint8_t raw = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (!gpio_get_level(PINS[i])) raw |= (1 << i);
    }
    uint8_t stable = (raw & s_prev) | (s_curr & ~(raw ^ s_prev));
    s_edge_down = stable & ~s_curr;
    s_edge_up   = ~stable &  s_curr;
    s_prev = raw;
    s_curr = stable;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
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
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return now - s_hold_start_ms[b];
}

bool input_any_pressed(void) { return s_edge_down != 0; }
