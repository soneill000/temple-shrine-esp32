// hw.h — screen geometry (universal) + badge pin map (ESP only).
// Landscape 320x240 (rotated 90° CW from panel-native portrait).
// See docs/pinout.md in the badge repo for the hardware details.

#pragma once

// --- Screen (used by both platforms) ---
#define SCREEN_W       320
#define SCREEN_H       240

// TempleOS canonical text grid: 8x8 font.
#define TEXT_COLS      40
#define TEXT_ROWS      30
#define GLYPH_W        8
#define GLYPH_H        8

// --- Pin map: ESP32-S3 only ---
#ifdef ESP_PLATFORM

#include "driver/gpio.h"

// Buttons (active-low, external 10K pullups)
#define PIN_BTN_LEFT   GPIO_NUM_3
#define PIN_BTN_UP     GPIO_NUM_4
#define PIN_BTN_DOWN   GPIO_NUM_5
#define PIN_BTN_RIGHT  GPIO_NUM_6
#define PIN_BTN_B      GPIO_NUM_7
#define PIN_BTN_A      GPIO_NUM_8
#define PIN_BTN_BOOT   GPIO_NUM_0

// Piezo (MOSFET-driven, ~4 kHz resonance)
#define PIN_BUZZER     GPIO_NUM_9
#define PIN_LED        GPIO_NUM_2

// Shared SPI bus
#define PIN_SPI_SCK    GPIO_NUM_13
#define PIN_SPI_MOSI   GPIO_NUM_11
#define PIN_SPI_MISO   GPIO_NUM_12

// ILI9341 TFT
#define PIN_TFT_CS     GPIO_NUM_47
#define PIN_TFT_DC     GPIO_NUM_40
#define PIN_TFT_RST    GPIO_NUM_41
#define TFT_SPI_HZ     40000000

// Other CS pins on shared bus (parked high at boot)
#define PIN_SD_CS      GPIO_NUM_10
#define PIN_LORA_CS    GPIO_NUM_48
#define PIN_TOUCH_CS   GPIO_NUM_14
#define PIN_MODSD_CS   GPIO_NUM_39
#define PIN_ACC_CS     GPIO_NUM_37

#endif  // ESP_PLATFORM
