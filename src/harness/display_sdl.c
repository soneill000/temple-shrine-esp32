// display_sdl.c — SDL2 backend for display.h.
//
// Strategy: keep a 320x240 RGB565 framebuffer in memory. Every drawing
// call writes directly into it. display_frame_end() converts the buffer
// to a texture and presents once. shrine_sleep_ms is the natural place
// to hook the present.

#include "display.h"
#include "hw.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#define ZOOM 3   // window pixel = ZOOM × logical pixel

static SDL_Window   *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture  *s_tex;
static uint16_t      s_fb[SCREEN_W * SCREEN_H];
static uint32_t      s_rgba[SCREEN_W * SCREEN_H];  // for texture upload

static inline uint32_t rgb565_to_rgba8888(uint16_t p)
{
    uint32_t r = (p >> 11) & 0x1F;
    uint32_t g = (p >> 5)  & 0x3F;
    uint32_t b =  p        & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void display_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        exit(1);
    }
    s_win = SDL_CreateWindow("TempleShrine",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * ZOOM, SCREEN_H * ZOOM,
        SDL_WINDOW_SHOWN);
    if (!s_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); exit(1); }

    s_ren = SDL_CreateRenderer(s_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); exit(1); }

    s_tex = SDL_CreateTexture(s_ren,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W, SCREEN_H);
    if (!s_tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); exit(1); }

    memset(s_fb, 0, sizeof(s_fb));
    display_frame_end();
}

static inline void put_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    s_fb[y * SCREEN_W + x] = color;
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        uint16_t *row = &s_fb[(y + yy) * SCREEN_W + x];
        for (int xx = 0; xx < w; xx++) row[xx] = color;
    }
}

void display_fill_screen(uint16_t color) { display_fill_rect(0, 0, SCREEN_W, SCREEN_H, color); }
void display_pixel(int x, int y, uint16_t color) { put_pixel(x, y, color); }

void display_rect(int x, int y, int w, int h, uint16_t color)
{
    display_fill_rect(x,         y,         w, 1, color);
    display_fill_rect(x,         y + h - 1, w, 1, color);
    display_fill_rect(x,         y,         1, h, color);
    display_fill_rect(x + w - 1, y,         1, h, color);
}

void display_hline(int x, int y, int w, uint16_t color) { display_fill_rect(x, y, w, 1, color); }
void display_vline(int x, int y, int h, uint16_t color) { display_fill_rect(x, y, 1, h, color); }

void display_blit(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H) return;
    for (int yy = 0; yy < h; yy++) {
        uint16_t *row = &s_fb[(y + yy) * SCREEN_W + x];
        for (int xx = 0; xx < w; xx++) row[xx] = pixels[yy * w + xx];
    }
}

void display_frame_end(void)
{
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        s_rgba[i] = rgb565_to_rgba8888(s_fb[i]);
    SDL_UpdateTexture(s_tex, NULL, s_rgba, SCREEN_W * sizeof(uint32_t));
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
}

void display_present_full(const uint16_t *pixels)
{
    memcpy(s_fb, pixels, sizeof(s_fb));
    display_frame_end();
}
