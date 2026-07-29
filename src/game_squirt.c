// game_squirt.c — port of Terry's Squirt.HC (fountain / hose physics toy).
//
// Terry's original uses his full ODE solver with CMass/CSpring across a
// chain of hose masses tethered to a faucet, plus separate droplet masses.
// We reproduce the essential setup with a hand-rolled mass-spring loop
// (explicit Euler + damping) — small enough to fit and fast enough for
// the badge SPI budget.
//
// Controls (Terry's are the same):
//   LEFT / RIGHT   move nozzle horizontally
//   UP   / DOWN    move nozzle vertically
//   A              (bonus) extra jet of droplets
//   BOOT           exit

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// Squirt used to run direct-to-display via shrine_*: every fill_rect,
// line, circle was its own SPI transaction. With ~300 draw calls per
// frame (hills + hose + drops + faucet), the game ran at single-digit
// fps. Now we compose the whole frame in scene_fb (PSRAM) and blit
// once per frame via display_present_full. Same pattern the After
// Egypt scenes use.
#include "scene_fb.h"
#define s_fb g_scene_fb

// Terry's Mountain sprite (BI=1 in Mountain.HC's tail): a 640x118
// SPT_BITMAP we already ship for After Egypt. Used here for Squirt's
// hills backdrop — writes go into s_fb, so the ~75K pixel iteration
// runs at PSRAM speed, not SPI-per-pixel like the earlier version.
#include "sprite_mountain.h"

// Second PSRAM framebuffer used as a static backdrop cache. Painted
// ONCE at scene entry with sky + hills + ground + faucet, then
// memcpy'd into s_fb at the start of every frame — so the mountain
// pixel iteration doesn't repeat and the frame time stays constant
// (avoids the gradual slowdown we were seeing).
#ifdef ESP_PLATFORM
  #include "esp_attr.h"
  EXT_RAM_BSS_ATTR static uint16_t s_backdrop[SCREEN_W * SCREEN_H];
#else
  static uint16_t s_backdrop[SCREEN_W * SCREEN_H];
#endif
static bool s_backdrop_built = false;

#define N_HOSE     12
#define N_DROPS    48

#define GROUND_Y   210
#define FAUCET_X   28
#define FAUCET_Y   36
#define NOZZLE_INIT_X  (SCREEN_W - 60)
#define NOZZLE_INIT_Y  180

#define GRAV       320.0f     // px / s^2
#define SPRING_K   40.0f      // spring stiffness
#define REST_LEN   14.0f
#define DAMP       0.985f     // per-frame velocity damping (approx)

typedef struct {
    float x, y;
    float vx, vy;
    bool  active;
} particle_t;

static particle_t s_hose[N_HOSE];   // 0 = faucet (fixed), N-1 = nozzle (player)
static particle_t s_drops[N_DROPS];
static int   s_next_drop;
static float s_nozzle_x, s_nozzle_y;
static float s_prev_nozzle_x, s_prev_nozzle_y;

static void reset(void)
{
    // Lay the hose in a lazy arc from faucet to nozzle.
    for (int i = 0; i < N_HOSE; i++) {
        float t = (float)i / (N_HOSE - 1);
        s_hose[i].x = FAUCET_X + t * (NOZZLE_INIT_X - FAUCET_X);
        s_hose[i].y = FAUCET_Y + t * (NOZZLE_INIT_Y - FAUCET_Y);
        s_hose[i].vx = s_hose[i].vy = 0;
        s_hose[i].active = true;
    }
    for (int i = 0; i < N_DROPS; i++) s_drops[i].active = false;
    s_next_drop = 0;
    s_nozzle_x = s_prev_nozzle_x = NOZZLE_INIT_X;
    s_nozzle_y = s_prev_nozzle_y = NOZZLE_INIT_Y;
}

static void step_physics(float dt)
{
    // Faucet is pinned. Nozzle position is set by the player before
    // step_physics runs (in the input handler); we treat it as a hard
    // constraint by snapping the last mass to the target.
    s_hose[N_HOSE - 1].x = s_nozzle_x;
    s_hose[N_HOSE - 1].y = s_nozzle_y;
    s_hose[N_HOSE - 1].vx = (s_nozzle_x - s_prev_nozzle_x) / dt;
    s_hose[N_HOSE - 1].vy = (s_nozzle_y - s_prev_nozzle_y) / dt;

    // Intermediate masses: gravity + spring toward each neighbor.
    for (int i = 1; i < N_HOSE - 1; i++) {
        float fx = 0, fy = GRAV;
        for (int neighbor_off = -1; neighbor_off <= 1; neighbor_off += 2) {
            int j = i + neighbor_off;
            float dx = s_hose[j].x - s_hose[i].x;
            float dy = s_hose[j].y - s_hose[i].y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 0.001f) len = 0.001f;
            float stretch = len - REST_LEN;
            float f = SPRING_K * stretch;
            fx += f * dx / len;
            fy += f * dy / len;
        }
        s_hose[i].vx = (s_hose[i].vx + fx * dt) * DAMP;
        s_hose[i].vy = (s_hose[i].vy + fy * dt) * DAMP;
        s_hose[i].x += s_hose[i].vx * dt;
        s_hose[i].y += s_hose[i].vy * dt;
    }

    // Droplets: gravity + ground.
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drops[i].active) continue;
        s_drops[i].vy += GRAV * dt;
        s_drops[i].x  += s_drops[i].vx * dt;
        s_drops[i].y  += s_drops[i].vy * dt;
        if (s_drops[i].y >= GROUND_Y || s_drops[i].x < 0 || s_drops[i].x >= SCREEN_W) {
            s_drops[i].active = false;
        }
    }

    s_prev_nozzle_x = s_nozzle_x;
    s_prev_nozzle_y = s_nozzle_y;
}

// Spawn a droplet at the nozzle, aimed along the last hose segment.
static void spawn_drop(void)
{
    float dx = s_hose[N_HOSE - 1].x - s_hose[N_HOSE - 2].x;
    float dy = s_hose[N_HOSE - 1].y - s_hose[N_HOSE - 2].y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) len = 0.001f;
    float speed = 90.0f + (float)shrine_god(50);
    // Small random spread.
    float ang_jitter = ((float)shrine_god(200) - 100.0f) / 400.0f;   // ±0.25 rad
    float c = cosf(ang_jitter), s = sinf(ang_jitter);
    float ux = ( dx * c - dy * s) / len;
    float uy = ( dx * s + dy * c) / len;
    for (int t = 0; t < 3; t++) {
        int i = s_next_drop;
        s_next_drop = (s_next_drop + 1) % N_DROPS;
        s_drops[i].x = s_hose[N_HOSE - 1].x;
        s_drops[i].y = s_hose[N_HOSE - 1].y;
        s_drops[i].vx = ux * speed + ((float)shrine_god(60) - 30);
        s_drops[i].vy = uy * speed + ((float)shrine_god(60) - 30);
        s_drops[i].active = true;
    }
}

// --- Framebuffer drawing helpers (compose into s_fb, blit once) ---

static inline uint16_t rgb(color_t c) { return PAL_RGB565[c & 15]; }

static inline void fb_pixel(int x, int y, color_t c)
{
    if ((unsigned)x < (unsigned)SCREEN_W && (unsigned)y < (unsigned)SCREEN_H)
        s_fb[y * SCREEN_W + x] = rgb(c);
}
static void fb_fill_rect(int x, int y, int w, int h, color_t c)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    uint16_t v = rgb(c);
    for (int j = 0; j < h; j++) {
        uint16_t *p = &s_fb[(y + j) * SCREEN_W + x];
        for (int i = 0; i < w; i++) p[i] = v;
    }
}
static inline void fb_hline(int x, int y, int w, color_t c) { fb_fill_rect(x, y, w, 1, c); }
static inline void fb_vline(int x, int y, int h, color_t c) { fb_fill_rect(x, y, 1, h, c); }
static void fb_rect(int x, int y, int w, int h, color_t c)
{
    fb_hline(x,         y,         w, c);
    fb_hline(x,         y + h - 1, w, c);
    fb_vline(x,         y,         h, c);
    fb_vline(x + w - 1, y,         h, c);
}
static void fb_line(int x0, int y0, int x1, int y1, color_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void fb_fill_circle(int cx, int cy, int r, color_t c)
{
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        fb_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
static void fb_circle(int cx, int cy, int r, color_t c)
{
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        fb_pixel(cx + x, cy + y, c); fb_pixel(cx - x, cy + y, c);
        fb_pixel(cx + x, cy - y, c); fb_pixel(cx - x, cy - y, c);
        fb_pixel(cx + y, cy + x, c); fb_pixel(cx - y, cy + x, c);
        fb_pixel(cx + y, cy - x, c); fb_pixel(cx - y, cy - x, c);
        x++;
        if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6;
    }
}
static void fb_putc(int x, int y, char ch, color_t fg, color_t bg)
{
    uint8_t code = (uint8_t)ch;
    if (code >= 128) code = ' ';
    const uint8_t *g = FONT8X8[code];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            fb_pixel(x + col, y + row, (bits & (1 << col)) ? fg : bg);
        }
    }
}
static void fb_puts(int col, int row, const char *s, color_t fg, color_t bg)
{
    int x = col * GLYPH_W, y = row * GLYPH_H;
    while (*s) { fb_putc(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static void fb_puts_centered(int row, const char *s, color_t fg, color_t bg)
{
    int n = 0; while (s[n]) n++;
    int x = (SCREEN_W - n * GLYPH_W) / 2;
    int y = row * GLYPH_H;
    while (*s) { fb_putc(x, y, *s++, fg, bg); x += GLYPH_W; }
}
static inline void fb_clear(color_t c)
{
    uint16_t v = rgb(c);
    int n = SCREEN_W * SCREEN_H;
    for (int i = 0; i < n; i++) s_fb[i] = v;
}

// --- Scene draw ---

static void draw_faucet(void)
{
    fb_fill_rect(FAUCET_X - 3, FAUCET_Y, 6, GROUND_Y - FAUCET_Y, C_DKGRAY);
    fb_rect    (FAUCET_X - 3, FAUCET_Y, 6, GROUND_Y - FAUCET_Y, C_BLACK);
    fb_fill_circle(FAUCET_X, FAUCET_Y, 5, C_LTGRAY);
    fb_circle    (FAUCET_X, FAUCET_Y, 5, C_BLACK);
}

static void draw_ground(void)
{
    fb_hline(0, GROUND_Y, SCREEN_W, C_YELLOW);
    fb_fill_rect(0, GROUND_Y + 1, SCREEN_W, SCREEN_H - GROUND_Y - 1, C_BROWN);
}

// Terry's Mountain BI=1 sprite (SPT_BITMAP 640x118), sampled 2:1 so
// its 640 Terry pixels land in our 320 fb pixels. Anchor sits so the
// bottom of the mountain silhouette lands right on the ground line at
// GROUND_Y. This is the "correct" hill art the user wanted back —
// what was slow before because it went pixel-by-pixel over SPI. Now
// it writes to s_fb (PSRAM), so the full 75K-pixel iteration is
// fast enough for real-time.
static void draw_hills_backdrop(void)
{
    // Header layout: 1 byte op + 4 i32 x + 4 i32 y + 4 i32 w + 4 i32 h.
    const uint8_t *px  = &SPRITE_MOUNTAIN_BI_1[17];
    const int      W   = 640;                // Terry width
    const int      H   = 118;                // Terry height
    const int      Ws  = 640;                // stride (already mult of 8)
    // Place mountain at the top of the playfield (below chrome text
    // rows 0..2). Sampled 2:1 the sprite is 320x59 fb pixels; anchor
    // y=20 puts its top edge just under the chrome and its bottom at
    // ~y=79, leaving the whole area below for the hose and drops.
    const int      dy0 = 20;
    for (int r = 0; r < H; r += 2) {
        int fy = dy0 + (r >> 1);
        if ((unsigned)fy >= (unsigned)SCREEN_H) continue;
        for (int c = 0; c < W; c += 2) {
            uint8_t p = px[r * Ws + c];
            if (p == 0xFF) continue;
            if (p < 16) fb_pixel(c >> 1, fy, (color_t)p);
        }
    }
}

static void draw_hose(void)
{
    for (int i = 0; i < N_HOSE - 1; i++) {
        int x1 = (int)s_hose[i].x,   y1 = (int)s_hose[i].y;
        int x2 = (int)s_hose[i+1].x, y2 = (int)s_hose[i+1].y;
        fb_line(x1, y1,     x2, y2,     C_GREEN);
        fb_line(x1, y1 - 1, x2, y2 - 1, C_GREEN);
        fb_line(x1, y1 + 1, x2, y2 + 1, C_GREEN);
    }
    fb_fill_circle((int)s_nozzle_x, (int)s_nozzle_y, 4, C_LTGREEN);
    fb_circle    ((int)s_nozzle_x, (int)s_nozzle_y, 4, C_BLACK);
}

static void draw_drops(void)
{
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drops[i].active) continue;
        int x = (int)s_drops[i].x, y = (int)s_drops[i].y;
        fb_pixel(x,     y,     C_LTCYAN);
        fb_pixel(x + 1, y,     C_LTCYAN);
        fb_pixel(x,     y + 1, C_WHITE);
    }
}

static void draw_chrome(void)
{
    fb_puts_centered(1, "*  SQUIRT  *", C_YELLOW, C_BG);
    fb_puts_centered(2, "AIM THE HOSE",  C_LTCYAN, C_BG);
    fb_puts_centered(TEXT_ROWS - 1,
                     "ARROWS AIM   A JET   BOOT EXIT",
                     C_LTGRAY, C_BG);
}

// Build the static backdrop (sky + hills + ground + faucet + chrome
// text) into s_backdrop once. Everything painted here is stationary
// and can be blitted verbatim each frame.
static void build_backdrop(void)
{
    // Sky (LTCYAN) above the mountain silhouette; yellow desert below.
    // Terry's Mountain sprite has transparent (0xFF) pixels around its
    // silhouette so the sky color shows through the top half.
    fb_fill_rect(0, 0,  SCREEN_W, 80, C_LTCYAN);
    fb_fill_rect(0, 80, SCREEN_W, GROUND_Y - 80, C_YELLOW);
    draw_hills_backdrop();
    draw_ground();
    draw_faucet();
    draw_chrome();
    // Snapshot s_fb into s_backdrop for the per-frame memcpy path.
    memcpy(s_backdrop, s_fb, SCREEN_W * SCREEN_H * sizeof(uint16_t));
    s_backdrop_built = true;
}

static void draw_playfield(void)
{
    if (!s_backdrop_built) build_backdrop();
    // Fast path: memcpy the cached static backdrop (sky + hills +
    // ground + faucet + chrome). Then overlay only the dynamic parts
    // (hose, drops, nozzle). Frame time is now constant regardless of
    // physics state, which fixes the gradual slowdown.
    memcpy(s_fb, s_backdrop, SCREEN_W * SCREEN_H * sizeof(uint16_t));
    draw_hose();
    draw_drops();
    display_present_full(s_fb);
}

void game_squirt_run(void)
{
    reset();
    s_backdrop_built = false;   // rebuild backdrop each fresh scene entry
    draw_playfield();   // paints chrome + backdrop + entities and blits

    uint32_t last = shrine_ms();
    uint32_t drop_accum = 0;

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        // Nozzle motion.
        const float speed = 90.0f;
        uint32_t now = shrine_ms();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        last = now;

        if (shrine_key_held(BTN_LEFT))  s_nozzle_x -= speed * dt;
        if (shrine_key_held(BTN_RIGHT)) s_nozzle_x += speed * dt;
        if (shrine_key_held(BTN_UP))    s_nozzle_y -= speed * dt;
        if (shrine_key_held(BTN_DOWN))  s_nozzle_y += speed * dt;
        if (s_nozzle_x < 8) s_nozzle_x = 8;
        if (s_nozzle_x > SCREEN_W - 8) s_nozzle_x = SCREEN_W - 8;
        if (s_nozzle_y < 28) s_nozzle_y = 28;
        if (s_nozzle_y > GROUND_Y - 6) s_nozzle_y = GROUND_Y - 6;

        step_physics(dt);

        drop_accum += (uint32_t)(dt * 1000);
        if (drop_accum >= 60 || shrine_key_pressed(BTN_A)) {
            drop_accum = 0;
            spawn_drop();
        }

        draw_playfield();
        shrine_sleep_ms(20);
    }
}
