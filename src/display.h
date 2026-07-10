// display.h — ILI9341 driver, landscape 320x240.
// Parks other CS pins on init so the shared SPI bus stays clean.
// All colors here are direct RGB565 values (native little-endian);
// higher-level code uses the 16-color palette in palette.h.

#pragma once

#include <stdint.h>
#include <stdbool.h>

void display_init(void);

// Called by shrine_sleep_ms on host builds so the SDL backend can present
// the framebuffer. On the badge this is a no-op — the ILI9341 gets pixels
// live via SPI.
void display_frame_end(void);

void display_fill_screen(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_pixel(int x, int y, uint16_t color);
void display_rect(int x, int y, int w, int h, uint16_t color);
void display_hline(int x, int y, int w, uint16_t color);
void display_vline(int x, int y, int h, uint16_t color);

// Blit a raw RGB565 bitmap (native endian) from SRAM/PSRAM.
void display_blit(int x, int y, int w, int h, const uint16_t *pixels);
