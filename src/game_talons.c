// game_talons.c — Tier 2 Talons: voxel-terrain flight demo.
//
// v2 changes:
//   1. Compose the frame in an off-screen RGB565 buffer, then push it
//      to the panel in ONE SPI transaction. The v1 render fired ~1500
//      tiny fill_rects per frame, each an SPI transaction whose overhead
//      dominated the actual pixel push. Now the transactions are ~1.
//   2. Direct RGB565 colors — not palette-indexed — with 8 height bands
//      and 4 distance bands (32 total), so we can express water-shallow-
//      grass-hills-rock-snow AND the atmospheric-perspective darkening
//      Terry gets from his 8-bit palette. Distant land shifts toward a
//      cold haze; near land stays vivid.
//   3. Small text renderer that writes directly into the framebuffer so
//      the HUD ships in the same present.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
  #include "esp_attr.h"
  #define FB_ATTR EXT_RAM_BSS_ATTR   // parks framebuffer in PSRAM
#else
  #define FB_ATTR
#endif

// --- Framebuffer ---
FB_ATTR static uint16_t s_fb[SCREEN_W * SCREEN_H];

// Build an RGB565 word from 5/6/5-bit channels.
#define RGB(r5, g6, b5) (uint16_t)(((r5) << 11) | ((g6) << 5) | (b5))

// --- Heightmap ---
#define HM_SIZE   128
#define HM_MASK   (HM_SIZE - 1)
static uint8_t s_hm[HM_SIZE * HM_SIZE];

static inline int hm_get(int wx, int wz)
{
    return s_hm[((wz & HM_MASK) << 7) | (wx & HM_MASK)];
}

// --- Terrain LUT: [height_band][distance_band] ---
// Height bands (H): 0 deep water, 1 shallows, 2 beach, 3 grass low,
// 4 grass hi, 5 foothills, 6 rock, 7 snow.
// Distance bands (D): 0 near, 1 mid, 2 far, 3 farthest — each column
// keeps its family (blues stay blue, greens stay green, etc.) and just
// dims. Previous version pulled everything toward a cold blue-gray at
// distance, which made mountains and sky and water all read the same.
static const uint16_t TERRAIN[8][4] = {
    /* 0 deep water   */ { RGB( 3,10,22), RGB( 3, 8,19), RGB( 3, 7,16), RGB( 3, 6,14) },
    /* 1 shallows     */ { RGB( 8,36,26), RGB( 7,30,22), RGB( 6,24,20), RGB( 5,18,18) },
    /* 2 beach        */ { RGB(30,44,14), RGB(26,38,12), RGB(22,32,12), RGB(18,26,12) },
    /* 3 grass low    */ { RGB( 8,48, 4), RGB( 6,40, 4), RGB( 5,32, 4), RGB( 5,26, 6) },
    /* 4 grass hi     */ { RGB(16,54,10), RGB(13,46, 8), RGB(11,38, 8), RGB(10,30, 8) },
    /* 5 foothills    */ { RGB(22,32,10), RGB(19,26, 8), RGB(16,22, 8), RGB(13,18, 8) },
    /* 6 rock         */ { RGB(22,22,16), RGB(18,18,14), RGB(15,15,12), RGB(12,12,10) },
    /* 7 snow         */ { RGB(31,62,30), RGB(28,55,28), RGB(24,48,24), RGB(20,40,20) },
};

static inline int height_band(int h)
{
    if (h <  40) return 0;
    if (h <  55) return 1;
    if (h <  65) return 2;
    if (h <  90) return 3;
    if (h < 120) return 4;
    if (h < 150) return 5;
    if (h < 195) return 6;
    return 7;
}

static inline int dist_band(float z)
{
    if (z < 12.0f) return 0;
    if (z < 28.0f) return 1;
    if (z < 55.0f) return 2;
    return 3;
}

// Sky gradient — warmer palette (dawn / haze) so it contrasts against
// terrain instead of matching the water and distant mountains. Top is a
// bruise-purple; the horizon washes toward a pale peach.
static uint16_t sky_color(int y, int horizon)
{
    if (horizon <= 0) return RGB(4, 4, 6);
    float t = (float)y / (float)horizon;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r = (int)( 6 + 24 * t);   // purple-red -> peach
    int g = (int)( 8 + 30 * t);   // dark -> light
    int b = (int)(12 + 10 * t);   // stays modest so it doesn't read as water
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return RGB(r, g, b);
}

// --- Heightmap generation ---
static void gen_heightmap(void)
{
    for (int z = 0; z < HM_SIZE; z++) {
        for (int x = 0; x < HM_SIZE; x++) {
            float fx = x, fz = z;
            float h = 92.0f;
            h += 60.0f * sinf(fx * 0.09f) * cosf(fz * 0.11f);
            h += 32.0f * sinf((fx + fz) * 0.17f);
            h += 16.0f * sinf(fx * 0.35f + fz * 0.29f);
            h +=  8.0f * cosf(fx * 0.7f);
            if (h < 0)   h = 0;
            if (h > 255) h = 255;
            s_hm[z * HM_SIZE + x] = (uint8_t)h;
        }
    }
}

// --- Framebuffer helpers ---

static inline void fb_pixel(int x, int y, uint16_t c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
        s_fb[y * SCREEN_W + x] = c;
}

static inline void fb_vline(int x, int y, int h, uint16_t c)
{
    if ((unsigned)x >= SCREEN_W) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    uint16_t *p = &s_fb[y * SCREEN_W + x];
    for (int i = 0; i < h; i++) { *p = c; p += SCREEN_W; }
}

static inline void fb_hline(int x, int y, int w, uint16_t c)
{
    if ((unsigned)y >= SCREEN_H) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    uint16_t *p = &s_fb[y * SCREEN_W + x];
    for (int i = 0; i < w; i++) p[i] = c;
}

static void fb_putc(int px, int py, char ch, uint16_t fg, uint16_t bg)
{
    uint8_t c = (uint8_t)ch;
    if (c >= 128) c = ' ';
    const uint8_t *g = FONT8X8[c];
    for (int r = 0; r < 8; r++) {
        uint8_t bits = g[r];
        for (int col = 0; col < 8; col++) {
            int x = px + col, y = py + r;
            if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
                s_fb[y * SCREEN_W + x] = (bits & (1 << col)) ? fg : bg;
        }
    }
}

static void fb_puts(int px, int py, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) { fb_putc(px, py, *s++, fg, bg); px += 8; }
}

static void fb_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Draw the mechanical grabbing talons — two arms extending down from the
// bottom center of the screen, reaching further and pinching in when
// fish are close.
static void render_talons(void)
{
    // Find nearest live fish (only if it's roughly in front of the plane).
    float cy_ = cosf(s_yaw), sy_ = sinf(s_yaw);
    float min_z = 1e9f;
    int   near_i = -1;
    for (int i = 0; i < N_FISH; i++) {
        if (s_fish[i].caught) continue;
        float dx = s_fish[i].x - s_cx;
        float dz = s_fish[i].z - s_cz;
        float cam_z = -dx * sy_ + dz * cy_;
        float cam_x =  dx * cy_ + dz * sy_;
        if (cam_z < 1.0f || cam_z > 12.0f) continue;
        if (cam_x * cam_x > cam_z * cam_z) continue;   // roughly in front
        if (cam_z < min_z) { min_z = cam_z; near_i = i; }
    }

    // Talons hang from the bottom center, base y just off the bottom.
    int base_cx = SCREEN_W / 2;
    int base_y  = SCREEN_H - 10;

    // How far to extend and how far apart the fingertips sit.
    float extend_t = 0.0f;      // 0 = resting; 1 = fully extended
    int   pinch    = 12;        // fingertip horizontal spread (px)
    if (near_i >= 0) {
        extend_t = 1.0f - (min_z / 12.0f);
        if (extend_t < 0) extend_t = 0;
        pinch = 12 - (int)(extend_t * 8);   // pinches together at close range
    }
    int extend_px = (int)(60.0f * extend_t);

    // Frame of each finger: base at bottom, elbow midway, tip near fish.
    for (int side = -1; side <= 1; side += 2) {
        int base_x   = base_cx + side * 14;
        int elbow_x  = base_cx + side * 20;
        int elbow_y  = base_y - extend_px / 2;
        int tip_x    = base_cx + side * pinch;
        int tip_y    = base_y - extend_px;
        // Upper arm (base -> elbow)
        fb_line(base_x - 1, base_y, elbow_x - 1, elbow_y, RGB(20, 22, 22));
        fb_line(base_x,     base_y, elbow_x,     elbow_y, RGB(28, 28, 28));
        fb_line(base_x + 1, base_y, elbow_x + 1, elbow_y, RGB(20, 22, 22));
        // Forearm (elbow -> tip)
        fb_line(elbow_x - 1, elbow_y, tip_x - 1, tip_y, RGB(20, 22, 22));
        fb_line(elbow_x,     elbow_y, tip_x,     tip_y, RGB(28, 28, 28));
        fb_line(elbow_x + 1, elbow_y, tip_x + 1, tip_y, RGB(20, 22, 22));
        // Claw fingers at the tip — three small hooks
        for (int f = 0; f < 3; f++) {
            int hx = tip_x + side * (f * 2);
            fb_line(hx, tip_y, hx + side * 2, tip_y + 4, RGB(31, 40, 8));
        }
    }
}

// --- Fish sprites ---
#define N_FISH  16
#define FISH_Y  40.0f    // world y of water surface
typedef struct { float x, z; bool caught; } fish_t;
static fish_t s_fish[N_FISH];
static int    s_caught;

static void place_fish(void)
{
    int placed = 0;
    for (int attempt = 0; placed < N_FISH && attempt < 400; attempt++) {
        int wx = (int)shrine_god(HM_SIZE);
        int wz = (int)shrine_god(HM_SIZE);
        if (s_hm[wz * HM_SIZE + wx] < 40) {
            s_fish[placed].x = (float)wx + 0.5f;
            s_fish[placed].z = (float)wz + 0.5f;
            s_fish[placed].caught = false;
            placed++;
        }
    }
    while (placed < N_FISH) {
        s_fish[placed].x = HM_SIZE / 2 + (float)((int)shrine_god(20) - 10);
        s_fish[placed].z = HM_SIZE / 2 + (float)((int)shrine_god(20) - 10);
        s_fish[placed].caught = false;
        placed++;
    }
}

// --- Camera / flight ---
static float s_cx, s_cy, s_cz;
static float s_yaw, s_pitch;
static float s_speed;
static float s_yaw_rate, s_pitch_rate;

#define FLY_SPEED     22.0f
#define BOOST_SPEED   38.0f
#define TURN_RATE     1.2f
#define PITCH_RATE    0.8f
#define GROUND_MARGIN 14.0f

static void reset_camera(void)
{
    s_cx = HM_SIZE / 2;
    s_cz = HM_SIZE / 2;
    s_cy = 190.0f;
    s_yaw = 0.0f;
    s_pitch = 0.0f;
    s_speed = FLY_SPEED;
    s_yaw_rate = s_pitch_rate = 0;
    s_caught = 0;
    for (int i = 0; i < N_FISH; i++) s_fish[i].caught = false;
}

// --- Voxel renderer ---
#define FOV_HALF   0.523f
#define NEAR_Z     1.0f
#define FAR_Z      140.0f          // further horizon — grow step keeps cost flat
#define BASE_HORIZON  120

static float s_col_tan[SCREEN_W];
static void build_col_tan(void)
{
    for (int x = 0; x < SCREEN_W; x++) {
        float u = ((float)x / (SCREEN_W - 1)) * 2.0f - 1.0f;
        s_col_tan[x] = u * tanf(FOV_HALF);
    }
}

// Per-column topmost drawn terrain y (for coarse sprite occlusion).
static int s_col_top[SCREEN_W];
static int s_caught;

// Render fish as scaled billboards after terrain, before HUD.
static void render_fish(int horizon)
{
    float cy_ = cosf(s_yaw), sy_ = sinf(s_yaw);
    float half_tan = tanf(FOV_HALF);
    for (int i = 0; i < N_FISH; i++) {
        if (s_fish[i].caught) continue;
        float dx = s_fish[i].x - s_cx;
        float dz = s_fish[i].z - s_cz;
        float cam_x =  dx * cy_ + dz * sy_;
        float cam_z = -dx * sy_ + dz * cy_;
        if (cam_z < 1.0f) continue;
        float tan_u = cam_x / cam_z;
        if (tan_u < -half_tan * 1.2f || tan_u > half_tan * 1.2f) continue;
        int sx = (int)((tan_u / half_tan + 1.0f) * (SCREEN_W - 1) / 2);
        float dy = FISH_Y - s_cy;
        int sy = horizon - (int)(dy / cam_z * 100.0f);
        int size = (int)(70.0f / cam_z);
        if (size < 2)  size = 2;
        if (size > 12) size = 12;
        // Coarse occlusion — if the whole sprite is above the terrain
        // silhouette at that column, don't draw.
        if (sx >= 0 && sx < SCREEN_W && sy + size < s_col_top[sx]) continue;

        // Fish body — ellipse; tail slightly darker; eye a bright dot.
        uint16_t body = RGB(30, 44, 6);
        uint16_t tail = RGB(24, 28, 4);
        uint16_t eye  = RGB(31, 62, 31);
        for (int py = -size / 2; py <= size / 2; py++) {
            for (int px = -size; px <= size; px++) {
                if (px * px * 4 + py * py * 16 > size * size * 4) continue;
                uint16_t c = (px < -size / 2) ? tail : body;
                fb_pixel(sx + px, sy + py, c);
            }
        }
        fb_pixel(sx + size / 2, sy - 1, eye);

        // "Catch" test — talons are almost fully extended (cam_z very
        // small) and we're pointed at the water. Terry's version deploys
        // automatically at close range; we do the same.
        if (cam_z < 3.5f) {
            s_fish[i].caught = true;
            s_caught++;
            shrine_beep(1800, 60);
            shrine_beep(2400, 80);
        }
    }
}

static void render_frame(void)
{
    int horizon = BASE_HORIZON + (int)(s_pitch * 240.0f);
    if (horizon < 0)   horizon = 0;
    if (horizon > SCREEN_H) horizon = SCREEN_H;

    // Sky gradient — one hline per row is fast in memory.
    for (int y = 0; y < horizon; y++)
        fb_hline(0, y, SCREEN_W, sky_color(y, horizon));

    // Prepare the below-horizon region: fill with a haze color so gaps
    // don't show through as garbage from the previous frame.
    uint16_t haze = TERRAIN[3][3];
    for (int y = horizon; y < SCREEN_H; y++)
        fb_hline(0, y, SCREEN_W, haze);

    float cos_y = cosf(s_yaw), sin_y = sinf(s_yaw);
    for (int x = 0; x < SCREEN_W; x++) {
        float ct = s_col_tan[x];
        float rx = ct * cos_y + sin_y;
        float rz = -ct * sin_y + cos_y;
        float rlen = sqrtf(rx * rx + rz * rz);
        rx /= rlen; rz /= rlen;

        int ymax = SCREEN_H;
        float z = NEAR_Z;
        float dz = 0.4f;
        while (z < FAR_Z && ymax > horizon) {
            float wx = s_cx + rx * z;
            float wz = s_cz + rz * z;
            int h = hm_get((int)wx, (int)wz);
            float dy = (float)h - s_cy;
            int screen_y = horizon - (int)(dy / z * 100.0f);
            if (screen_y < ymax) {
                if (screen_y < horizon) screen_y = horizon;
                uint16_t c = TERRAIN[height_band(h)][dist_band(z)];
                fb_vline(x, screen_y, ymax - screen_y, c);
                ymax = screen_y;
            }
            // Aggressive step growth — near sampling stays dense so nearby
            // features look sharp; far sampling coarsens so total iteration
            // count stays bounded (~90 per column) even at FAR_Z=140.
            z  += dz;
            dz += 0.045f;
        }
        s_col_top[x] = ymax;   // record for sprite occlusion
    }
}

// --- HUD ---
static const uint16_t HUD_BG = RGB(2, 4, 6);   // near-black
static const uint16_t HUD_FG = RGB(31, 60, 8); // amber-yellow
static const uint16_t HUD_ACC = RGB(28, 40, 30);
static const uint16_t HUD_HIGHLIGHT = RGB(31, 40, 31);

static void render_hud(uint32_t t_ms)
{
    // Top strip
    for (int y = 0; y < 16; y++) fb_hline(0, y, SCREEN_W, HUD_BG);
    // Bottom strip
    for (int y = SCREEN_H - 10; y < SCREEN_H; y++) fb_hline(0, y, SCREEN_W, HUD_BG);

    char buf[24];
    int heading = ((int)(s_yaw * 180.0f / 3.14159f) % 360 + 360) % 360;
    int pitch_deg = (int)(s_pitch * 180.0f / 3.14159f);

    snprintf(buf, sizeof(buf), "HDG %03d", heading);
    fb_puts(2, 0, buf, HUD_FG, HUD_BG);
    snprintf(buf, sizeof(buf), "ALT %03d", (int)s_cy);
    fb_puts(2, 8, buf, HUD_FG, HUD_BG);
    snprintf(buf, sizeof(buf), "PIT %+03d", pitch_deg);
    fb_puts(SCREEN_W - 8 * 8, 0, buf, HUD_FG, HUD_BG);
    snprintf(buf, sizeof(buf), "FSH %02d/%02d", s_caught, N_FISH);
    fb_puts(SCREEN_W - 9 * 8, 8, buf, HUD_HIGHLIGHT, HUD_BG);

    // Blinking title in the middle-top.
    const char *title = "* TALONS *";
    int title_w = 10 * 8;
    uint16_t tcol = ((t_ms / 500) & 1) ? HUD_HIGHLIGHT : HUD_ACC;
    fb_puts((SCREEN_W - title_w) / 2, 0, title, tcol, HUD_BG);

    // Crosshair.
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    fb_hline(cx - 6, cy, 4, HUD_FG);
    fb_hline(cx + 3, cy, 4, HUD_FG);
    fb_vline(cx, cy - 4, 3, HUD_FG);
    fb_vline(cx, cy + 2, 3, HUD_FG);

    // Control hint.
    fb_puts(2, SCREEN_H - 8, "ARROWS FLY  A DIVE  BOOT EXIT",
            HUD_ACC, HUD_BG);
}

// --- Update ---
static void update_camera(float dt, bool boost)
{
    s_yaw   += s_yaw_rate   * dt;
    s_pitch += s_pitch_rate * dt;
    s_yaw_rate   *= 0.90f;
    s_pitch_rate *= 0.92f;
    if (s_pitch >  0.7f) s_pitch =  0.7f;
    if (s_pitch < -0.7f) s_pitch = -0.7f;
    if (s_yaw >  6.2832f) s_yaw -= 6.2832f;
    if (s_yaw < -6.2832f) s_yaw += 6.2832f;

    float target = boost ? BOOST_SPEED : FLY_SPEED;
    s_speed += (target - s_speed) * dt * 3.0f;

    s_cx += sinf(s_yaw) * cosf(s_pitch) * s_speed * dt;
    s_cz += cosf(s_yaw) * cosf(s_pitch) * s_speed * dt;
    s_cy -= sinf(s_pitch) * s_speed * dt;

    int h_below = hm_get((int)s_cx, (int)s_cz);
    if (s_cy < (float)h_below + GROUND_MARGIN)
        s_cy = (float)h_below + GROUND_MARGIN;
    if (s_cy > 250.0f) s_cy = 250.0f;
}

void game_talons_run(void)
{
    gen_heightmap();
    build_col_tan();
    place_fish();
    reset_camera();

    uint32_t last = shrine_ms();
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (shrine_key_held(BTN_LEFT))  s_yaw_rate   -= TURN_RATE  * 0.20f;
        if (shrine_key_held(BTN_RIGHT)) s_yaw_rate   += TURN_RATE  * 0.20f;
        if (shrine_key_held(BTN_UP))    s_pitch_rate -= PITCH_RATE * 0.20f;
        if (shrine_key_held(BTN_DOWN))  s_pitch_rate += PITCH_RATE * 0.20f;
        bool boost = shrine_key_held(BTN_A);

        uint32_t now = shrine_ms();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.08f) dt = 0.08f;
        last = now;

        update_camera(dt, boost);
        int horizon = BASE_HORIZON + (int)(s_pitch * 240.0f);
        if (horizon < 0)   horizon = 0;
        if (horizon > SCREEN_H) horizon = SCREEN_H;
        render_frame();
        render_fish(horizon);
        render_talons();
        render_hud(now);

        display_present_full(s_fb);

        // No shrine_sleep_ms — the SPI push is the natural frame gate.
    }
}
