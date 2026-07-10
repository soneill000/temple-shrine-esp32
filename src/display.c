// display.c — ILI9341 SPI driver (adapted from the Tetris firmware).

#include "display.h"
#include "hw.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"

#define CHUNK_PIXELS 512

static spi_device_handle_t s_tft;
static uint16_t *s_chunk;

static void tft_dc(int level) { gpio_set_level(PIN_TFT_DC, level); }

static void tft_cmd(uint8_t c)
{
    tft_dc(0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &c };
    spi_device_polling_transmit(s_tft, &t);
}

static void tft_data(const uint8_t *d, size_t n)
{
    if (!n) return;
    tft_dc(1);
    spi_transaction_t t = { .length = 8 * n, .tx_buffer = d };
    spi_device_polling_transmit(s_tft, &t);
}

static void tft_data1(uint8_t v) { tft_data(&v, 1); }

static void set_window(int x, int y, int w, int h)
{
    uint16_t x1 = x, x2 = x + w - 1;
    uint16_t y1 = y, y2 = y + h - 1;
    uint8_t buf[4];

    tft_cmd(0x2A);
    buf[0] = x1 >> 8; buf[1] = x1 & 0xFF;
    buf[2] = x2 >> 8; buf[3] = x2 & 0xFF;
    tft_data(buf, 4);

    tft_cmd(0x2B);
    buf[0] = y1 >> 8; buf[1] = y1 & 0xFF;
    buf[2] = y2 >> 8; buf[3] = y2 & 0xFF;
    tft_data(buf, 4);

    tft_cmd(0x2C);
    tft_dc(1);
}

static void stream_solid(uint16_t color, int count)
{
    uint16_t be = (color >> 8) | (color << 8);
    for (int i = 0; i < CHUNK_PIXELS; i++) s_chunk[i] = be;

    while (count > 0) {
        int n = count > CHUNK_PIXELS ? CHUNK_PIXELS : count;
        spi_transaction_t t = { .length = 16 * n, .tx_buffer = s_chunk };
        spi_device_polling_transmit(s_tft, &t);
        count -= n;
    }
}

static void stream_bitmap(const uint16_t *pixels, int count)
{
    while (count > 0) {
        int n = count > CHUNK_PIXELS ? CHUNK_PIXELS : count;
        for (int i = 0; i < n; i++) {
            uint16_t p = pixels[i];
            s_chunk[i] = (p >> 8) | (p << 8);
        }
        spi_transaction_t t = { .length = 16 * n, .tx_buffer = s_chunk };
        spi_device_polling_transmit(s_tft, &t);
        pixels += n;
        count  -= n;
    }
}

static void ili9341_init_sequence(void)
{
    gpio_set_level(PIN_TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_cmd(0xCB); { uint8_t d[]={0x39,0x2C,0x00,0x34,0x02}; tft_data(d,5); }
    tft_cmd(0xCF); { uint8_t d[]={0x00,0xC1,0x30};           tft_data(d,3); }
    tft_cmd(0xE8); { uint8_t d[]={0x85,0x00,0x78};           tft_data(d,3); }
    tft_cmd(0xEA); { uint8_t d[]={0x00,0x00};                tft_data(d,2); }
    tft_cmd(0xED); { uint8_t d[]={0x64,0x03,0x12,0x81};      tft_data(d,4); }
    tft_cmd(0xF7); tft_data1(0x20);
    tft_cmd(0xC0); tft_data1(0x23);
    tft_cmd(0xC1); tft_data1(0x10);
    tft_cmd(0xC5); { uint8_t d[]={0x3E,0x28}; tft_data(d,2); }
    tft_cmd(0xC7); tft_data1(0x86);
    tft_cmd(0x36); tft_data1(0x28);   // MADCTL: BGR + landscape (MV, 90° CW)
    tft_cmd(0x3A); tft_data1(0x55);
    tft_cmd(0xB1); { uint8_t d[]={0x00,0x18}; tft_data(d,2); }
    tft_cmd(0xB6); { uint8_t d[]={0x08,0x82,0x27}; tft_data(d,3); }
    tft_cmd(0xF2); tft_data1(0x00);
    tft_cmd(0x26); tft_data1(0x01);
    tft_cmd(0xE0); { uint8_t d[]={0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                                  0x37,0x07,0x10,0x03,0x0E,0x09,0x00}; tft_data(d,15); }
    tft_cmd(0xE1); { uint8_t d[]={0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                                  0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F}; tft_data(d,15); }

    tft_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    tft_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
}

void display_init(void)
{
    // Park all other SPI devices' CS lines high.
    const gpio_num_t others[] = {
        PIN_SD_CS, PIN_LORA_CS, PIN_TOUCH_CS, PIN_MODSD_CS, PIN_ACC_CS
    };
    for (unsigned i = 0; i < sizeof(others)/sizeof(others[0]); i++) {
        gpio_reset_pin(others[i]);
        gpio_set_direction(others[i], GPIO_MODE_OUTPUT);
        gpio_set_level(others[i], 1);
    }

    gpio_reset_pin(PIN_TFT_DC);
    gpio_set_direction(PIN_TFT_DC, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_TFT_RST);
    gpio_set_direction(PIN_TFT_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_TFT_RST, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_PIXELS * 2 + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = TFT_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_TFT_CS,
        .queue_size = 4,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_tft));

    s_chunk = heap_caps_malloc(CHUNK_PIXELS * 2, MALLOC_CAP_DMA);

    ili9341_init_sequence();
    display_fill_screen(0x0000);
}

void display_fill_screen(uint16_t color)
{
    display_fill_rect(0, 0, SCREEN_W, SCREEN_H, color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x >= SCREEN_W || y >= SCREEN_H) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    set_window(x, y, w, h);
    stream_solid(color, w * h);
}

void display_pixel(int x, int y, uint16_t color)
{
    display_fill_rect(x, y, 1, 1, color);
}

void display_rect(int x, int y, int w, int h, uint16_t color)
{
    display_fill_rect(x,         y,         w, 1, color);
    display_fill_rect(x,         y + h - 1, w, 1, color);
    display_fill_rect(x,         y,         1, h, color);
    display_fill_rect(x + w - 1, y,         1, h, color);
}

void display_hline(int x, int y, int w, uint16_t color)
{
    display_fill_rect(x, y, w, 1, color);
}

void display_vline(int x, int y, int h, uint16_t color)
{
    display_fill_rect(x, y, 1, h, color);
}

void display_frame_end(void) { /* live SPI on badge — nothing to flush */ }

void display_present_full(const uint16_t *pixels)
{
    set_window(0, 0, SCREEN_W, SCREEN_H);
    stream_bitmap(pixels, SCREEN_W * SCREEN_H);
}

void display_blit(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H) return;
    set_window(x, y, w, h);
    stream_bitmap(pixels, w * h);
}
