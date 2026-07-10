// main.c — Boot: splash -> menu -> games -> menu (loop).

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "hw.h"
#include "shrine.h"
#include "splash.h"
#include "menu.h"

void app_main(void)
{
    // Heartbeat LED
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LED, 1);

    printf("TempleShrine booting...\n");
    shrine_init();

    // A quick "boot chime" — three ascending tones.
    shrine_beep(392, 120);
    shrine_beep(523, 120);
    shrine_beep(784, 200);

    while (1) {
        splash_run();
        menu_run();
    }
}
