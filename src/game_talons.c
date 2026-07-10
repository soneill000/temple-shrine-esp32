// game_talons.c — Tier 2 Talons: flight over voxel terrain.
//
// Not Terry's original — his Talons is a 900-line polygon-3D flight sim
// (heightfield tri strips, depth buffer, matrix math, fish sprites,
// deployable talons). To make it fit in what our badge can actually
// render in real time, we swap the polygon rasterizer for a Comanche-
// style columnar voxel raycaster: 320 vertical scans per frame, each
// walking outward from the camera and drawing a strip per depth step.
//
// The game feel and camera / control scheme match Terry's:
//   UP / DOWN     pitch (climb / dive)
//   LEFT / RIGHT  yaw (turn)
//   A             boost dive
//   BOOT          exit
//
// This is the "flight over terrain" milestone. Fish, catching talons,
// and the scoring loop are stubs to be fleshed out on top.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// --- Heightmap ---
#define HM_SIZE     128            // power of 2 for cheap wrap-around
#define HM_MASK     (HM_SIZE - 1)
static uint8_t s_hm[HM_SIZE * HM_SIZE];

static inline int hm_get(int wx, int wz)
{
    return s_hm[((wz & HM_MASK) << 7) | (wx & HM_MASK)];
}

// Terrain colors keyed by height band. Kept in the 16-color VGA palette
// so nothing feels off-key against the rest of the shrine.
static color_t terrain_color(int h)
{
    if (h < 40)  return C_BLUE;      // water (matches BG)
    if (h < 55)  return C_LTBLUE;    // shallows
    if (h < 65)  return C_BROWN;     // beach
    if (h < 110) return C_GREEN;     // grass
    if (h < 150) return C_LTGREEN;   // hills lit
    if (h < 190) return C_BROWN;     // rock
    return C_WHITE;                  // snow
}

static void gen_heightmap(void)
{
    // Sum-of-sines terrain: cheap, deterministic, gives believable hills
    // + lakes without needing Perlin noise or diamond-square. Two long
    // waves lay down the continental shape, two short ones add ripples.
    for (int z = 0; z < HM_SIZE; z++) {
        for (int x = 0; x < HM_SIZE; x++) {
            float fx = x, fz = z;
            float h = 90.0f;
            h += 55.0f * sinf(fx * 0.09f) * cosf(fz * 0.11f);
            h += 35.0f * sinf((fx + fz) * 0.17f);
            h += 15.0f * sinf(fx * 0.35f + fz * 0.29f);
            h +=  8.0f * cosf(fx * 0.7f);
            if (h < 0)   h = 0;
            if (h > 255) h = 255;
            s_hm[z * HM_SIZE + x] = (uint8_t)h;
        }
    }
}

// --- Camera ---
static float s_cx, s_cy, s_cz;        // world position
static float s_yaw, s_pitch;
static float s_speed;
static float s_yaw_rate, s_pitch_rate; // stick-style continuous inputs

#define FLY_SPEED       24.0f
#define BOOST_SPEED     40.0f
#define TURN_RATE       1.2f           // radians / sec
#define PITCH_RATE      0.8f
#define GROUND_MARGIN   14.0f

static void reset_camera(void)
{
    s_cx = HM_SIZE / 2;
    s_cz = HM_SIZE / 2;
    s_cy = 180.0f;
    s_yaw   = 0.0f;
    s_pitch = 0.0f;
    s_speed = FLY_SPEED;
    s_yaw_rate = s_pitch_rate = 0;
}

// --- Voxel renderer ---
// FOV = 60°. Precompute the horizontal tan for each column into a small
// LUT so the inner loop stays fp-cheap.
#define FOV_HALF   0.523f              // ~30° each side
#define NEAR_Z     1.0f
#define FAR_Z      80.0f
#define HORIZON_PX 120                  // resting horizon y on screen
static float s_col_tan[SCREEN_W];

static void build_col_tan(void)
{
    for (int x = 0; x < SCREEN_W; x++) {
        float u = ((float)x / (SCREEN_W - 1)) * 2.0f - 1.0f;   // -1..+1
        s_col_tan[x] = u * tanf(FOV_HALF);
    }
}

// Draw one frame: sky, terrain (via 320 columns of vertical strips), and
// a very thin HUD overlay left over from the previous frame is repainted
// by the caller.
static void render_frame(void)
{
    // Sky — flat blue block above the horizon line (moves with pitch).
    int horizon = HORIZON_PX + (int)(s_pitch * 240.0f);
    if (horizon < 0)   horizon = 0;
    if (horizon > SCREEN_H) horizon = SCREEN_H;
    shrine_fill_rect(0, 0, SCREEN_W, horizon, C_DKGRAY);       // sky
    shrine_fill_rect(0, horizon - 1, SCREEN_W, 2, C_LTGRAY);   // horizon band

    // Terrain columns.
    float cos_y = cosf(s_yaw), sin_y = sinf(s_yaw);
    for (int x = 0; x < SCREEN_W; x++) {
        // Ray direction in world XZ plane.
        float ct = s_col_tan[x];
        float rx = ct * cos_y + sin_y;
        float rz = -ct * sin_y + cos_y;
        // Normalize so distance == world units traveled.
        float rlen = sqrtf(rx * rx + rz * rz);
        rx /= rlen; rz /= rlen;

        int ymax = SCREEN_H;
        // March outward with growing step (more detail near, coarser far).
        float z = NEAR_Z;
        float dz = 0.5f;
        while (z < FAR_Z && ymax > horizon) {
            float wx = s_cx + rx * z;
            float wz = s_cz + rz * z;
            int h = hm_get((int)wx, (int)wz);
            float dy = (float)h - s_cy;
            // Perspective: y_on_screen = horizon - dy / z * K
            int screen_y = horizon - (int)(dy / z * 100.0f);
            if (screen_y < ymax) {
                if (screen_y < horizon) screen_y = horizon;
                color_t c = terrain_color(h);
                shrine_vline(x, screen_y, ymax - screen_y, c);
                ymax = screen_y;
            }
            z  += dz;
            dz += 0.02f;   // grow step
        }
    }
}

// --- HUD ---
static void render_hud(uint32_t t_ms)
{
    char buf[24];
    // Left: heading (compass degrees) + altitude.
    int heading = (int)((s_yaw * 180.0f / 3.14159f) + 360) % 360;
    int altitude = (int)s_cy;
    snprintf(buf, sizeof(buf), "HDG %03d", heading);
    shrine_puts(0, 0, buf, C_YELLOW, C_DKGRAY);
    snprintf(buf, sizeof(buf), "ALT %03d", altitude);
    shrine_puts(0, 1, buf, C_YELLOW, C_DKGRAY);

    // Right: pitch indicator + speed.
    int pitch_deg = (int)(s_pitch * 180.0f / 3.14159f);
    snprintf(buf, sizeof(buf), "PIT %+03d", pitch_deg);
    shrine_puts(TEXT_COLS - 8, 0, buf, C_YELLOW, C_DKGRAY);
    snprintf(buf, sizeof(buf), "SPD %03d", (int)s_speed);
    shrine_puts(TEXT_COLS - 8, 1, buf, C_YELLOW, C_DKGRAY);

    // Center: cross-hair and small pitch ladder.
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    shrine_hline(cx - 6, cy, 4, C_YELLOW);
    shrine_hline(cx + 3, cy, 4, C_YELLOW);
    shrine_vline(cx, cy - 4, 3, C_YELLOW);
    shrine_vline(cx, cy + 2, 3, C_YELLOW);

    // Blinking star at top-center to prove tick freshness.
    if ((t_ms / 500) & 1) shrine_puts(TEXT_COLS / 2 - 3, 0, "TALONS",
                                       C_LTMAGENTA, C_DKGRAY);
    else                  shrine_puts(TEXT_COLS / 2 - 3, 0, "TALONS",
                                       C_LTCYAN, C_DKGRAY);

    // Bottom: control hint.
    shrine_fill_rect(0, TEXT_ROWS - 1, SCREEN_W, GLYPH_H, PAL_RGB565[C_BLACK]);
    shrine_puts(0, TEXT_ROWS - 1,
                "ARROWS FLY   A DIVE   BOOT EXIT",
                C_LTGRAY, C_BLACK);
}

// --- Update ---
static void update_camera(float dt, bool boost)
{
    // Apply stick rates.
    s_yaw   += s_yaw_rate   * dt;
    s_pitch += s_pitch_rate * dt;
    // Damp/return-to-center so the plane self-levels a bit.
    s_yaw_rate   *= 0.90f;
    s_pitch_rate *= 0.92f;
    // Clamp pitch to avoid gimbal-adjacent weirdness.
    if (s_pitch > 0.7f)  s_pitch = 0.7f;
    if (s_pitch < -0.7f) s_pitch = -0.7f;
    // Wrap yaw.
    if (s_yaw > 6.2832f)  s_yaw -= 6.2832f;
    if (s_yaw < -6.2832f) s_yaw += 6.2832f;

    // Target speed.
    float target = boost ? BOOST_SPEED : FLY_SPEED;
    s_speed += (target - s_speed) * dt * 3.0f;

    // Forward motion in world XZ, and vertical motion from pitch.
    s_cx += sinf(s_yaw) * cosf(s_pitch) * s_speed * dt;
    s_cz += cosf(s_yaw) * cosf(s_pitch) * s_speed * dt;
    s_cy -= sinf(s_pitch) * s_speed * dt;

    // Ground safety: don't sink below the terrain.
    int h_below = hm_get((int)s_cx, (int)s_cz);
    if (s_cy < (float)h_below + GROUND_MARGIN)
        s_cy = (float)h_below + GROUND_MARGIN;
    if (s_cy > 245.0f) s_cy = 245.0f;
}

void game_talons_run(void)
{
    gen_heightmap();
    build_col_tan();
    reset_camera();
    shrine_clear(C_BLACK);

    uint32_t last = shrine_ms();
    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        // Continuous input into "stick rates" so control feels smooth.
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
        render_frame();
        render_hud(now);

        shrine_sleep_ms(15);
    }
}
