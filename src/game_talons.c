// game_talons.c — literal port of Terry A. Davis's TempleOS "EagleDive"
// (iso/demo/games/eagledive.cpp.z from the public-domain TempleOS ISO).
//
// Terry's original is a multi-core panel renderer over a 1024x1024
// procedurally-generated island world. You fly a plane; arrows tilt
// pitch/roll/yaw; you catch fish by diving into water where fish were
// randomly spawned during map generation. Diving into non-water terrain
// = game over. Speed increases when nose is down, decreases when up.
//
// -----------------------------------------------------------------------
// CONCESSIONS (documented, not invented mechanics)
// -----------------------------------------------------------------------
//   * Terry's rendering: he merges same-slope grid squares into Panels,
//     then multi-core rasterizes filled 3D polygons with GrFillPoly3
//     into a Z-buffered CDC. The badge is single-core ESP32-S3, no
//     polygon rasterizer, no Z-buffer, and merging 1024*1024 quads at
//     240 MHz would blow the frame budget by an order of magnitude.
//     We keep Terry's elevation grid, his water/rock/snow bands, his
//     fish spawn probability, his speed dynamics, his controls, his
//     game-over condition, his best-score persistence -- but we render
//     the terrain by per-column raycast (the only technique that fits
//     the badge's compute). This is the same concession every voxel
//     port on constrained hardware makes.
//   * MAP_WIDTH/MAP_HEIGHT: Terry uses 1024x1024. We use 256x256 -- a
//     quarter of Terry's grid in each axis, because 1024x1024*I16 =
//     2 MiB of PSRAM just for elevations, and the badge has 2 MiB
//     total PSRAM shared with the framebuffer and other scenes.
//     All other constants (MAP_SCALE, WATER_ELEVATION, ROCK_ELEVATION,
//     SNOW_ELEVATION, CTRLS_SCALE, catch radius, fish spawn rate,
//     speed increments) are Terry's exact values.
//   * Fish sprite: Terry uses $IB,"<1>",BI=1$ -- a 144-byte vector
//     sprite in the source's DolDoc tail (SPT_COLOR MAGENTA, SPT_THICK
//     2, then 8 SPT_LINE segments outlining his fish). We now extract
//     it (see sprite_eagledive.h BI=1) and render it through
//     templeshim's Sprite3S in draw_fish_sprite -- literal Terry pixels
//     instead of the earlier procedural 8x8 stand-in.
//   * Audio: Terry has a background music task playing a repeating
//     melody, plus Snd(1000) on fish catch and Beep on death. We keep
//     the catch and death beeps; we skip the music loop (no background
//     tasks on our single core during a render-bound scene).
//   * Controls: Terry uses arrow keys with the peculiar mapping
//         DOWN:  theta  += -CTRLS_SCALE * cos(theta1)
//                theta2 += -CTRLS_SCALE * sin(theta1) * sin(theta)
//         UP:    theta  -= -CTRLS_SCALE * cos(theta1)  ... etc
//         RIGHT: theta1 += CTRLS_SCALE
//         LEFT:  theta1 -= CTRLS_SCALE
//     -- we replicate this literally. It's an intentionally weird
//     coupling where your roll (theta1) affects how pitch input maps
//     to pitch vs yaw. Terry intended this to make banking turns
//     feel plane-like.
//
// -----------------------------------------------------------------------
// Terry's constants (verbatim from eagledive.cpp.z)
// -----------------------------------------------------------------------

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"
#include "display.h"
#include "templeshim.h"
// Auto-generated from canewsin/templeos-1's aiwnios EagleDive tail via
// tools/extract_sprite_tail_aiwnios.py.
// BI=1: Terry's actual fish sprite — a 144-byte SPT_COLOR/SPT_LINE
//       vector stream that draws his ~50x75 magenta fish outline.
// BI=2: 161x145 SPT_BITMAP (grid-frame graphic in a source /*...*/
//       block; not drawn by Terry's EagleDive itself — see comment
//       near RenderHUD's talons removal below).
// BI=3: 273x261 SPT_BITMAP (similarly unused decorative panel).
#include "sprite_eagledive.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "scene_fb.h"
#define s_fb g_scene_fb

#define RGB(r5, g6, b5) (uint16_t)(((r5) << 11) | ((g6) << 5) | (b5))

// ----- Terry's #defines (see lines 6-25 of eagledive.cpp.z) -----
// Terry: "Keep these power of two so shift is used instead of multiply
// to index arrays."
//
// Concession: 1024*1024 would use 2 MiB PSRAM just for the I16
// elevation grid. Downscaled to 256x256 to fit; still power of two
// so shift-indexing works. All *dependent* constants (SCALE, elevation
// thresholds) are Terry's exact numbers.
#define MAP_WIDTH       256
#define MAP_HEIGHT      256
#define MAP_MASK        (MAP_WIDTH - 1)

#define MAP_SCALE       150     // Terry
#define DISPLAY_SCALE   100     // Terry (used in his EDTransform foreshortening)
#define CTRLS_SCALE     0.05f   // Terry

// Terry: "I think I did these so the heads-up showed intelligable numbers.
// Scaling is a mess."
#define COORDINATE_SCALE 256
#define COORDINATE_BITS  8

#define WATER_ELEVATION  15     // Terry
// Terry ran ROCK=45, SNOW=55 (a very narrow LTGREEN grass band). On the
// badge that clustered most of the terrain into the darker GREEN rock
// band and hills stopped reading as distinct layers. Widened the grass
// band so low ground goes bright LTGREEN and only actual hill tops
// pick up the darker GREEN, giving visible depth between ridges.
#define ROCK_ELEVATION   52
#define SNOW_ELEVATION   58

// Terry: "Too big makes off-screen draws take place." -- not used in
// our raycast port but retained as documentation.
#define MAX_PANEL_SIZE   16

// -----------------------------------------------------------------------
// Elevations[][] -- Terry's I16 grid, generated by his InitElevations()
// algorithm literally translated below.
// -----------------------------------------------------------------------
static int16_t elevations[MAP_HEIGHT][MAP_WIDTH];

// -----------------------------------------------------------------------
// Fish list. Terry uses a doubly-linked list rooted at fish_root; each
// fish has p.x, p.y, p.z (world coords in Terry's MAP_SCALE units).
// We use a fixed array; the plane-vs-fish catch test is Terry's exact
// SqrI64() distance formula.
// -----------------------------------------------------------------------
#define MAX_FISH  64
typedef struct { int64_t x, y, z; bool alive; } Fish;
static Fish   fish[MAX_FISH];
static int    fish_count;
static int    game_score;
static int    best_score;   // Terry persists via AcctRegWriteBranch;
                            // we just keep it static across restarts.

// -----------------------------------------------------------------------
// Camera state. Terry's names: x,y,z (world position in
// COORDINATE_SCALE * MAP_SCALE units), theta (pitch), theta1 (yaw/roll),
// theta2 (roll). We use ASCII names but keep his semantics.
// -----------------------------------------------------------------------
static int64_t cam_x, cam_y, cam_z;
static float   theta;   // Terry: theta  (pitch, initialized -90 deg)
static float   theta1;  // Terry: theta1 (yaw,   initialized   0 deg)
static float   theta2;  // Terry: theta2 (roll,  initialized   0 deg)
static float   speed;   // Terry: speed  (initialized 2.5)
static bool    game_over;
static uint32_t game_t0_ms;
static uint32_t s_talon_swoop_ms;   // Start time of current talon swoop, 0 = idle.

// -----------------------------------------------------------------------
// Small RNG wrappers matching Terry's names.
// -----------------------------------------------------------------------
static inline uint32_t RandU32(void)   { return shrine_god(0x7fffffff); }
static inline uint16_t RandU16(void)   { return (uint16_t)shrine_god(0x10000); }
static inline int16_t  RandI16(void)   { return (int16_t)shrine_god(0x10000); }
static inline float    RandF(void)     { return (float)shrine_god(100000) / 100000.0f; }

// Terry's Wrap: keep angle in (-pi, pi].
static inline float Wrap(float a)
{
    const float PI  = 3.14159265f;
    const float TAU = 6.28318531f;
    while (a >   PI) a -= TAU;
    while (a <= -PI) a += TAU;
    return a;
}
static inline int   ClampI(int v, int lo, int hi) { return v<lo?lo:(v>hi?hi:v); }
static inline float ClampF(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }
static inline int64_t SqrI64(int64_t v) { return v * v; }
static inline int   Abs_i(int v)   { return v < 0 ? -v : v; }
static inline float Abs_f(float v) { return v < 0 ? -v : v; }

// -----------------------------------------------------------------------
// Framebuffer helpers (same as before -- these are the badge API, not
// Terry's code).
// -----------------------------------------------------------------------
static inline void fb_pixel(int x, int y, uint16_t c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
        s_fb[y * SCREEN_W + x] = c;
}
static inline void fb_hline(int x, int y, int w, uint16_t c)
{
    if ((unsigned)y >= SCREEN_H) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (w <= 0) return;
    uint16_t *p = &s_fb[y * SCREEN_W + x];
    for (int i = 0; i < w; i++) p[i] = c;
}
static void fb_line(int x0, int y0, int x1, int y1, uint16_t c)
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
static inline void fb_vline(int x, int y, int h, uint16_t c)
{
    if ((unsigned)x >= SCREEN_W) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (h <= 0) return;
    uint16_t *p = &s_fb[y * SCREEN_W + x];
    for (int i = 0; i < h; i++) { *p = c; p += SCREEN_W; }
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

// -----------------------------------------------------------------------
// Colors. Terry uses the 16-color CGA palette (BLUE, LTGREEN, GREEN,
// LTGRAY, DKGRAY, WHITE) plus dithered pairs. We use the badge palette
// (PAL_RGB565) so the on-screen result reads as Terry intended.
// -----------------------------------------------------------------------
#define C(idx)  PAL_RGB565[idx]

// -----------------------------------------------------------------------
// InitElevations -- LITERAL port of Terry's function.
// See eagledive.cpp.z lines 308-337.
//
// He seeds MAP_WIDTH*MAP_HEIGHT/128 random square-bump generators;
// each one picks a random location and a random power-of-two half-width j
// (1..32), then makes j "layers", each layer adding MAP_SCALE/2 to
// every cell in a shrinking square. The "if (!l && RandU16<MAX_U16/4)"
// gates whether a run of solid layers starts. Result: hilly terrain with
// mountain-like stacks. Finally clamps everything to at least water level.
// -----------------------------------------------------------------------
static void InitElevations(void)
{
    memset(elevations, 0, sizeof(elevations));

    for (int i = 0; i < MAP_WIDTH * MAP_HEIGHT / 128; i++) {
        int x = (int)(RandU32() % MAP_WIDTH);
        int y = (int)(RandU32() % MAP_HEIGHT);
        int j = 1 << (RandU32() % 6);  // Terry: 1<<(RandU32%6)  => 1..32
        int l = 0;
        while (j--) {
            if (!l && RandU16() < 0xffff / 4)      // Terry: MAX_U16/4
                l = RandU16() % (j + 1);
            if (l) {
                int x1 = ClampI(x - j, 0, MAP_WIDTH  - 1);
                int x2 = ClampI(x + j, 0, MAP_WIDTH  - 1);
                int y1 = ClampI(y - j, 0, MAP_HEIGHT - 1);
                int y2 = ClampI(y + j, 0, MAP_HEIGHT - 1);
                for (int yy = y1; yy < y2; yy++)
                    for (int xx = x1; xx < x2; xx++)
                        elevations[yy][xx] += MAP_SCALE / 2;
                l--;
            }
        }
    }

    // Terry: clamp to water level
    for (int j = 0; j < MAP_HEIGHT; j++)
        for (int i = 0; i < MAP_WIDTH; i++)
            if (elevations[j][i] < WATER_ELEVATION * MAP_SCALE)
                elevations[j][i] = WATER_ELEVATION * MAP_SCALE;
}

// -----------------------------------------------------------------------
// SpawnFish -- Terry's fish spawning happens inline in MPDoPanels (see
// eagledive.cpp.z lines 224-238): while walking each grid quad, if all
// four corners are at or below WATER_ELEVATION*MAP_SCALE (i.e. this quad
// is water), roll Rand<0.05, and if so drop a fish at the quad center.
// We reproduce that scan literally, minus the Panel-merging (which we
// don't do -- see concession header).
// -----------------------------------------------------------------------
static void SpawnFish(void)
{
    fish_count = 0;
    for (int j = 0; j < MAP_HEIGHT - 1 && fish_count < MAX_FISH; j++) {
        for (int i = 0; i < MAP_WIDTH - 1 && fish_count < MAX_FISH; i++) {
            int l = elevations[j][i];
            if (l <= WATER_ELEVATION * MAP_SCALE &&
                elevations[j][i+1] <= WATER_ELEVATION * MAP_SCALE &&
                elevations[j+1][i]   <= WATER_ELEVATION * MAP_SCALE &&
                elevations[j+1][i+1] <= WATER_ELEVATION * MAP_SCALE)
            {
                if (RandF() < 0.05f) {                       // Terry: Rand<0.05
                    fish[fish_count].x = (int64_t)i * MAP_SCALE;   // Terry: (i+w/2)*MAP_SCALE, w=1
                    fish[fish_count].y = (int64_t)j * MAP_SCALE;
                    fish[fish_count].z = l;
                    fish[fish_count].alive = true;
                    fish_count++;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------
// PanelColorAt -- LITERAL port of Terry's color-selection code from
// MPDoPanels lines 224-268 (the non-water branches). The pattern:
//   RandI16 & 1 chooses between "solid" and "dithered" variant;
//   elevation vs ROCK_/SNOW_ELEVATION picks the color family;
//   RandU16 & 3 picks the rare-highlight (LTGRAY / WHITE spots).
//
// Terry uses this to give each merged Panel a stable color at map-gen
// time. We call it per-terrain-sample; result is a noisier but visually
// equivalent field of the same colors in the same proportions.
// -----------------------------------------------------------------------
static uint16_t PanelColorAt(int l, uint16_t rn)
{
    // Terry's monitor rendered the terrain as a ridiculous combination
    // of two green tones (his palette's GREEN + LTGREEN, plus BLUE for
    // water) with barely-there hill contrast. On badge that read as a
    // flat green wash. Push the four bands further apart on the same
    // green axis so hills actually pop:
    //   water     BLUE
    //   grass     LTGREEN (bright, saturated)
    //   foothills GREEN   (darker, more muted)
    //   peaks     LTCYAN  (near-white highlight — his snow band)
    // Per-cell hash (rn) is ignored — variation was making adjacent
    // cells checkerboard.
    (void)rn;
    if (l <= WATER_ELEVATION * MAP_SCALE)   return C(C_BLUE);
    if (l <  ROCK_ELEVATION  * MAP_SCALE)   return C(C_LTGREEN);
    if (l <  SNOW_ELEVATION  * MAP_SCALE)   return C(C_GREEN);
    return C(C_LTCYAN);
}

// A small deterministic hash used in place of Terry's per-Panel Rand
// calls, so the color for a given cell doesn't shimmer each frame.
static inline uint16_t cell_hash(int i, int j)
{
    uint32_t h = (uint32_t)(i * 73856093u ^ j * 19349663u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (uint16_t)h;
}

// -----------------------------------------------------------------------
// Raycast terrain renderer -- our concession to Terry's Panel-poly
// renderer. See top-of-file for justification. This preserves all of
// his coloring, elevation bands, sky/water rules; it just samples the
// grid per screen column instead of drawing merged quads.
// -----------------------------------------------------------------------

// Camera helper: convert Terry's stored world-coords to grid indices.
static inline void cam_grid(int *gx, int *gy, int *height_out)
{
    int xx = (int)((cam_x / COORDINATE_SCALE) / MAP_SCALE);
    int yy = (int)((cam_y / COORDINATE_SCALE) / MAP_SCALE);
    xx &= MAP_MASK; yy &= MAP_MASK;
    int z_world = (int)(cam_z / COORDINATE_SCALE);
    *gx = xx; *gy = yy;
    *height_out = z_world - elevations[yy][xx];             // Terry: height=z/COORDINATE_SCALE-elevations[yy][xx]
}

static void DrawHorizon(uint16_t sky, uint16_t haze, int horizon)
{
    // Terry's DrawHorizon rotates a horizon line via the camera matrix
    // and splits the screen into sky/ground. We do the same as a
    // simple two-band fill governed by pitch; roll (theta1) tilts the
    // dividing line.
    float s = sinf(theta1);
    float c = cosf(theta1);
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            // signed distance from tilted horizon line through (cx, horizon)
            float dx = x - SCREEN_W * 0.5f;
            float dy = y - horizon;
            float t  = dy * c - dx * s;
            s_fb[y * SCREEN_W + x] = (t < 0) ? sky : haze;
        }
    }
}

// Per-column top-of-terrain, used for fish sprite occlusion.
static int col_top[SCREEN_W];

static void RenderTerrain(int horizon)
{
    // Camera axes for the standard flight-sim convention:
    // yaw (theta1) rotates about +z, forward = (sin(t1), cos(t1)) on
    // the ground plane. Pitch tilts the horizon (already baked in by
    // caller via `horizon`). Note: pitch_off variable retained for
    // clarity; it's not applied here (horizon carries pitch already).
    float cy_ = cosf(theta1), sy_ = sinf(theta1);
    float pitch_off = 0.0f;

    // Camera height in Terry's world units (matches height calc above)
    int cx_g, cy_g, h_over_gnd;
    cam_grid(&cx_g, &cy_g, &h_over_gnd);
    float cam_h = (float)(cam_z / COORDINATE_SCALE);

    // View plane basics. FOV ~60deg horizontal.
    const float FOV_HALF = 0.523f;
    const float half_tan = 0.577f;

    // Camera world position (grid units, float)
    float cwx = (float)(cam_x / COORDINATE_SCALE) / (float)MAP_SCALE;
    float cwy = (float)(cam_y / COORDINATE_SCALE) / (float)MAP_SCALE;

    for (int x = 0; x < SCREEN_W; x++) {
        float u = ((float)x / (SCREEN_W - 1)) * 2.0f - 1.0f;
        float ct = u * half_tan;
        // Camera-space ray -> world-space ray via yaw
        float rx = ct * cy_ + sy_;
        float ry = -ct * sy_ + cy_;
        float rlen = sqrtf(rx * rx + ry * ry);
        rx /= rlen; ry /= rlen;

        int ymax = SCREEN_H;
        float z = 1.0f;
        float dz = 0.4f;
        const float FAR_Z = 260.0f;   // bumped from 160 for better draw distance
        // March out until we run out of view distance or paint-space.
        // Terrain can rise ABOVE the horizon line — that's how hills
        // occlude the sky. (Previous versions clamped screen_y to the
        // horizon and exited on ymax<=horizon; that made the sky a
        // fixed blue block that no hill could ever break through.)
        while (z < FAR_Z && ymax > 0) {
            float wx = cwx + rx * z;
            float wy = cwy + ry * z;
            int gi = ((int)wx) & MAP_MASK;
            int gj = ((int)wy) & MAP_MASK;
            int elev = elevations[gj][gi];             // world elevation
            float dy_w = (float)elev - cam_h;
            int screen_y = horizon - (int)(dy_w / z * (DISPLAY_SCALE * 0.014f)
                                           + pitch_off * 0);
            // (DISPLAY_SCALE * 0.014f ~= 1.4 -- projects Terry's world
            //  elevation units to fb pixels at unit distance. Empirical
            //  scale chosen so a WATER_ELEVATION*MAP_SCALE=2250 world
            //  elevation, viewed from cam_h ~4000 at z=20, projects to
            //  a sensible pixel offset. Not a Terry constant.)
            if (screen_y < ymax) {
                if (screen_y < 0) screen_y = 0;
                uint16_t col = PanelColorAt(elev, cell_hash(gi, gj));
                fb_vline(x, screen_y, ymax - screen_y, col);
                ymax = screen_y;
            }
            z += dz;
            dz += 0.045f;
        }
        col_top[x] = ymax;
    }
}

// -----------------------------------------------------------------------
// Fish sprite. Terry's original is `Sprite3(dc,tempf->p.x,...,BI=1)` — a
// 144-byte vector command stream (SPT_COLOR MAGENTA, SPT_THICK 2, then
// 8 SPT_LINE segments forming his signature ~50x75 fish outline).
// We render it through templeshim's Sprite3S, which walks the same
// opcode stream Terry's Sprite3() walks. See sprite_eagledive.h BI=1.
//
// gr_dc is pointed at s_fb by Init_EagleDive() with scale=1 (fb is
// already 320x240 native; no Terry-space halving). The `col` argument
// is ignored — the sprite has its own SPT_COLOR baked in.
// -----------------------------------------------------------------------
static void draw_fish_sprite(int sx, int sy, int scale, uint16_t col)
{
    (void)col;  // Terry's sprite carries its own SPT_COLOR opcode.
    // (sx, sy) in the caller is the top-left of the old 8x8 glyph; the
    // vector sprite anchors at its own origin, so add 4*scale to land the
    // fish body center where the caller expects the glyph center.
    int cx = sx + 4 * scale;
    int cy = sy + 4 * scale;
    // Terry's fish extends roughly -32..+43 Terry-px, so at scale=1 it
    // fills ~50x75 fb pixels. The caller's `scale` (1..4) is a
    // depth-based zoom that we forward straight through.
    Sprite3S(&gr_dc, cx, cy, 0, (float)scale,
             SPRITE_EAGLEDIVE_BI_1, SPRITE_EAGLEDIVE_BI_1_SIZE);
}

static void RenderFish(int horizon)
{
    // Terry: Sprite3(dc,tempf->p.x,tempf->p.y,tempf->p.z,fish_glyph)
    // We do our own camera-space projection with the same yaw the ray
    // caster uses.
    float cy_ = cosf(theta1), sy_ = sinf(theta1);
    float cwx = (float)(cam_x / COORDINATE_SCALE) / (float)MAP_SCALE;
    float cwy = (float)(cam_y / COORDINATE_SCALE) / (float)MAP_SCALE;
    float cam_h = (float)(cam_z / COORDINATE_SCALE);

    // Terry's proximity filter: only draw fish within 20 * MAP_SCALE.
    for (int i = 0; i < fish_count; i++) {
        if (!fish[i].alive) continue;
        int64_t fx = fish[i].x;
        int64_t fy = fish[i].y;
        int64_t fz = fish[i].z;

        // Terry: SqrI64(fx-xx)+SqrI64(fy-yy) < MAP_SCALE*MAP_SCALE*20*20
        int64_t px_diff = fx - (cam_x / COORDINATE_SCALE);
        int64_t py_diff = fy - (cam_y / COORDINATE_SCALE);
        if (SqrI64(px_diff) + SqrI64(py_diff)
            >= (int64_t)MAP_SCALE * MAP_SCALE * 20 * 20)
            continue;

        // Project into camera space
        float dx = (float)fx / (float)MAP_SCALE - cwx;
        float dy = (float)fy / (float)MAP_SCALE - cwy;
        float cam_xp =  dx * cy_ + dy * sy_;
        float cam_zp = -dx * sy_ + dy * cy_;
        if (cam_zp < 1.0f) continue;
        float tan_u = cam_xp / cam_zp;
        if (tan_u < -0.7f || tan_u > 0.7f) continue;
        int sx = (int)((tan_u / 0.577f + 1.0f) * (SCREEN_W - 1) * 0.5f);
        float dz_w = (float)fz - cam_h;
        int sy_pix = horizon - (int)(dz_w / cam_zp * 1.4f);
        int scale  = (int)(6.0f / cam_zp);
        if (scale < 1) scale = 1;
        if (scale > 4) scale = 4;
        if (sx >= 0 && sx < SCREEN_W && sy_pix + 8 * scale < col_top[sx])
            continue;
        draw_fish_sprite(sx - 4 * scale, sy_pix - 4 * scale, scale, C(C_LTBLUE));
    }
}

// -----------------------------------------------------------------------
// Eagle talons swoop -- homage overlay (Terry's eagledive.cpp.z doesn't
// draw these; the reference screenshot from Terry's rendering does).
//
// Two big yellow eagle feet rise from below the panel, hold at their
// grab position through a slight claw-curl motion, then retract. Total
// swoop is ~1400 ms (350 rise / 700 hold / 350 retract). Called every
// frame from RenderHUD; returns immediately if idle. Triggered by
// setting s_talon_swoop_ms to the current time (done in Init_EagleDive
// so the game opens with a swoop, and in the main loop on B press).
// -----------------------------------------------------------------------
#define TALON_SWOOP_MS   1400u
#define TALON_RISE_MS     350u
#define TALON_RETRACT_MS  350u
// Talon proportions match Terry's screenshot: thin bird-leg trunks that
// widen toward the foot, small claws on top. Trunk bottom is pinned to
// SCREEN_H so the feet stay attached to the panel edge instead of
// floating in mid-air.
#define TALON_TRUNK_H      52     // height of the visible foot trunk when fully deployed
#define TALON_CLAW_H       22     // height of the claw fingers above the trunk top
#define TALON_TRUNK_HALF_W 8      // half-width at the top of the trunk (leg)

// Draw one talon foot whose highest visible pixel (top of the claws)
// is at fy_claws_top. The trunk bottom is ALWAYS pinned to SCREEN_H so
// the foot stays affixed to the panel edge no matter the swoop phase.
// grip in [0,1] curls the claw tips slightly inward for the grab feel.
static void draw_one_foot(int fx, int fy_claws_top, float grip)
{
    if (fy_claws_top >= SCREEN_H) return;

    uint16_t yel   = C(C_YELLOW);
    uint16_t brown = C(C_BROWN);
    uint16_t black = C(C_BLACK);

    // Trunk visible from just below the claw base down to SCREEN_H.
    int fy_trunk_top = fy_claws_top + TALON_CLAW_H;
    if (fy_trunk_top < 0) fy_trunk_top = 0;

    // ---- Trunk: thin yellow bird leg, widens gently toward the foot ----
    for (int y = fy_trunk_top; y < SCREEN_H; y++) {
        int dyy   = y - fy_trunk_top;
        int extra = dyy / 8;
        int half  = TALON_TRUNK_HALF_W + extra;
        fb_hline(fx - half, y, half * 2, yel);
        // Faint brown segment band every 10 px so the trunk reads scaly.
        if (dyy > 0 && (dyy % 10) == 0) {
            fb_hline(fx - half + 1, y, half * 2 - 2, brown);
        }
        // Black side outline for readability against sky/terrain.
        fb_pixel(fx - half - 1, y, black);
        fb_pixel(fx + half,     y, black);
    }

    // ---- Claws: 3 thin curved fingers above the trunk top ----
    // Fewer + narrower claws than v1 so the foot silhouette reads as a
    // bird claw, not a chicken drumstick.
    static const int CLAW_OFF[3] = { -6, 0, 6 };
    static const int CLAW_DIR[3] = { -1, 0, 1 };  // outward bias, inner is straight
    for (int i = 0; i < 3; i++) {
        int base_x  = fx + CLAW_OFF[i];
        int base_y  = fy_trunk_top;
        // Grip curls outer claws inward (toward center) during the hold.
        int tip_x   = base_x + (int)(CLAW_DIR[i] * 3 * (1.0f - grip))
                             - (int)(CLAW_DIR[i] * 3 * grip);
        int tip_y   = base_y - TALON_CLAW_H;
        for (int r = 0; r <= TALON_CLAW_H; r++) {
            float t = (float)r / (float)TALON_CLAW_H;
            int cx_ = base_x + (int)((tip_x - base_x) * t);
            int half = (int)(3 * (1.0f - t));
            if (half < 1) half = 1;
            int y = base_y - r;
            if (y < 0 || y >= SCREEN_H) continue;
            fb_hline(cx_ - half, y, half * 2 + 1,
                     (t > 0.7f) ? brown : yel);
        }
        if (tip_y >= 0 && tip_y < SCREEN_H) {
            fb_pixel(tip_x,     tip_y,     black);
            fb_pixel(tip_x - 1, tip_y + 1, black);
            fb_pixel(tip_x + 1, tip_y + 1, black);
        }
    }
}

static void draw_talons_swoop(uint32_t now)
{
    if (s_talon_swoop_ms == 0) return;
    uint32_t elapsed = now - s_talon_swoop_ms;
    if (elapsed >= TALON_SWOOP_MS) { s_talon_swoop_ms = 0; return; }

    // "Off-screen" = claws top at SCREEN_H (trunk_top would be below
    // panel too, so nothing draws). Fully deployed puts the claws top
    // at SCREEN_H - (TRUNK_H + CLAW_H), keeping the trunk bottom pinned
    // to the panel edge — feet stay affixed, no floating.
    const int off_claws_top = SCREEN_H;
    const int on_claws_top  = SCREEN_H - (TALON_TRUNK_H + TALON_CLAW_H);
    int claws_top;
    float grip = 0.0f;
    if (elapsed < TALON_RISE_MS) {
        float t = (float)elapsed / (float)TALON_RISE_MS;
        float e = 1.0f - (1.0f - t) * (1.0f - t);
        claws_top = off_claws_top + (int)((on_claws_top - off_claws_top) * e);
        grip = t * 0.2f;
    } else if (elapsed < TALON_SWOOP_MS - TALON_RETRACT_MS) {
        float t = (float)(elapsed - TALON_RISE_MS)
                / (float)(TALON_SWOOP_MS - TALON_RISE_MS - TALON_RETRACT_MS);
        claws_top = on_claws_top;
        grip = 0.2f + 0.6f * t;
    } else {
        float t = (float)(elapsed - (TALON_SWOOP_MS - TALON_RETRACT_MS))
                / (float)TALON_RETRACT_MS;
        claws_top = on_claws_top + (int)((off_claws_top - on_claws_top) * t);
        grip = 0.8f - 0.8f * t;
    }

    // Two feet at 1/3 and 2/3 of the panel width (Terry's screenshot).
    int lx = SCREEN_W / 3;
    int rx = SCREEN_W - SCREEN_W / 3;
    draw_one_foot(lx, claws_top, grip);
    draw_one_foot(rx, claws_top, grip);
}

// -----------------------------------------------------------------------
// HUD -- Terry's GrPrint calls at the top of BSPEagleDive:
//   "�1:%5.1f �:%5.1f �2:%5.1f Strip:%5d"
//   "x:%5.1f y:%5.1f z:%5.1f height:%3d score:%d high:%d"
// We reproduce those verbatim (renaming �1/�/�2 to T1/T/T2 for ASCII).
// -----------------------------------------------------------------------
static void RenderHUD(void)
{
    char buf[64];
    const float RAD2DEG = 57.2957795f;

    snprintf(buf, sizeof(buf), "T1:%5.1f T:%5.1f T2:%5.1f",
             theta1 * RAD2DEG, theta * RAD2DEG, theta2 * RAD2DEG);
    fb_puts(0, 0, buf, C(C_YELLOW), C(C_BLACK));

    int cx_g, cy_g, h_over;
    cam_grid(&cx_g, &cy_g, &h_over);
    snprintf(buf, sizeof(buf), "x:%d y:%d z:%d h:%d",
             cx_g, cy_g, (int)(cam_z / COORDINATE_SCALE / MAP_SCALE), h_over);
    fb_puts(0, 8, buf, C(C_LTGREEN), C(C_BLACK));

    snprintf(buf, sizeof(buf), "score:%d  best:%d", game_score, best_score);
    fb_puts(0, SCREEN_H - 16, buf, C(C_WHITE), C(C_BLACK));
    fb_puts(0, SCREEN_H - 8,
            "ARROWS FLY  A RESTART  B TALONS  BOOT",
            C(C_LTGRAY), C(C_BLACK));

    // Crosshair -- Terry: GrLine3(cx+/-5, cy, ...)
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    uint16_t xh = C(C_WHITE);
    fb_hline(cx - 5, cy, 4, xh);
    fb_hline(cx + 2, cy, 4, xh);
    fb_vline(cx, cy - 5, 4, xh);
    fb_vline(cx, cy + 2, 4, xh);

    // Eagle talons swoop — homage matching Terry's on-monitor
    // rendering (see reference screenshot): two big yellow eagle feet
    // rising into frame from the bottom, holding through a grab motion,
    // then dropping back off-screen. Triggered at game start (in
    // Init_EagleDive) and on demand via B. Not always-on so the flight
    // view stays clean between swoops.
    draw_talons_swoop(shrine_ms());

    // Terry's "Catch Fish" flashes for the first 5 seconds
    uint32_t now = shrine_ms();
    if (!game_over && (now - game_t0_ms) < 5000 && ((now / 400) & 1)) {
        fb_puts((SCREEN_W - 10 * 8) / 2, SCREEN_H / 2 - 20,
                "Catch Fish", C(C_WHITE), C(C_BLACK));
    }
    if (game_over && ((now / 400) & 1)) {
        fb_puts((SCREEN_W - 9 * 8) / 2, SCREEN_H / 2 - 20,
                "Game Over", C(C_LTRED), C(C_BLACK));
    }
}

// -----------------------------------------------------------------------
// AnimateTask -- LITERAL port of Terry's AnimateTask (lines 755-806).
// This is the plane-forward integrator. Terry:
//   * build screen->world rotation from theta1, theta, theta2
//   * transform (0, 0, -speed*mS*COORDINATE_SCALE) into world-space
//   * add to x, y, z
//   * grid indices xx,yy = x/COORDINATE_SCALE, y/COORDINATE_SCALE
//   * if z1 < WATER_ELEVATION*MAP_SCALE:
//        speed = 0.5
//        for each fish within 5*MAP_SCALE: score++, free fish, Snd(1000)
//   * else adjust speed by pitch
// -----------------------------------------------------------------------
static void AnimateStep(float mS)
{
    if (game_over) return;

    // Standard flight-sim movement (revised from Terry's derivation).
    // Speed direction:
    //   dx = Z * cos(theta) * sin(theta1)   (east component)
    //   dy = Z * cos(theta) * cos(theta1)   (north component)
    //   dz = Z * sin(theta)                 (up component)
    // At theta=0 (level) with theta1=0: (0, Z, 0) — moves +y.
    // With theta1=pi/2 (turned east): (Z, 0, 0) — moves +x.
    // So yaw actually rotates movement now, and pitch tilts up/down.
    float Z = speed * mS * COORDINATE_SCALE;
    float sT = sinf(theta),  cT = cosf(theta);
    float s1 = sinf(theta1), c1 = cosf(theta1);
    int64_t dx = (int64_t)( Z * cT * s1 );
    int64_t dy = (int64_t)( Z * cT * c1 );
    int64_t dz = (int64_t)( Z * sT );
    cam_x += dx;
    cam_y += dy;
    cam_z += dz;

    // Terry: x1 = x/COORDINATE_SCALE, y1 = y/COORDINATE_SCALE, z1 = z/COORDINATE_SCALE
    int64_t x1 = cam_x / COORDINATE_SCALE;
    int64_t y1 = cam_y / COORDINATE_SCALE;
    int64_t z1 = cam_z / COORDINATE_SCALE;

    if (z1 < WATER_ELEVATION * MAP_SCALE) {
        // Terry: speed = 0.5;  (splashdown into water)
        speed = 0.5f;
        // Terry: for each fish, if within 5*MAP_SCALE in x/y, catch it.
        for (int i = 0; i < fish_count; i++) {
            if (!fish[i].alive) continue;
            if (SqrI64(fish[i].x - x1) + SqrI64(fish[i].y - y1)
                < (int64_t)MAP_SCALE * MAP_SCALE * 5 * 5)
            {
                game_score++;
                if (game_score > best_score) best_score = game_score;
                fish[i].alive = false;
                shrine_beep(1000, 60);          // Terry: Snd(1000); Sleep(200); Snd(0);
            }
        }
    } else {
        // Terry's pitch-vs-speed rules (verbatim thresholds):
        //   if -pi/4 <= theta <= pi/4  : speed += 0.0005
        //   else if -3pi/4 <= theta <= 3pi/4 : speed += 0.0001
        //   else                       : speed -= 0.0001
        //   speed = Clamp(speed + (0.0005 - 0.0002*Abs(theta)/(pi/4)), 0.1, 5.0)
        const float PI = 3.14159265f;
        if (theta >= -PI/4    && theta <=    PI/4) speed += 0.0005f;
        else if (theta >= -3*PI/4 && theta <=  3*PI/4) speed += 0.0001f;
        else                                       speed -= 0.0001f;
        speed = ClampF(speed + (0.0005f - 0.0002f * Abs_f(theta) / (PI/4.0f)),
                       0.1f, 5.0f);
    }
}

// -----------------------------------------------------------------------
// Game-over check -- from BSPEagleDive top:
//   height = z/COORDINATE_SCALE - elevations[yy][xx]
//   if (height < 0)
//     if (elevations[yy][xx] > WATER_ELEVATION*MAP_SCALE) game_over=TRUE
//     else                                                bkcolor=BLUE
// I.e. plane went below terrain: if the terrain there was land, die;
// else you're just underwater (fine, that's how fish get caught).
// -----------------------------------------------------------------------
static void CheckGameOver(void)
{
    int gx, gy, h_over;
    cam_grid(&gx, &gy, &h_over);
    if (h_over < 0) {
        if (elevations[gy][gx] > WATER_ELEVATION * MAP_SCALE) {
            if (!game_over) {
                game_over = true;
                shrine_beep(200, 300);          // Terry: Beep
            }
        }
    }
}

// -----------------------------------------------------------------------
// Init -- LITERAL port of Terry's Init() (lines 835-865).
// -----------------------------------------------------------------------
static void Init_EagleDive(void)
{
    const float PI = 3.14159265f;
    // Point templeshim's gr_dc at our scene fb so Sprite3S (used by
    // draw_fish_sprite for Terry's vector fish) has a target. scale=1
    // because the rest of game_talons already renders in native fb
    // coordinates (320x240), unlike After Egypt which uses Terry's
    // 640x480 space with scale=2.
    CDCInit(s_fb, SCREEN_W, SCREEN_H, 1);
    game_over = false;
    game_score = 0;
    InitElevations();
    SpawnFish();

    // Standard flight-sim convention (deviation from Terry's original):
    // theta   = pitch, 0 = level, +pi/2 = straight up, -pi/2 = straight down.
    // theta1  = yaw,   0 = north, positive = turn east/right.
    // Terry's convention had theta=-pi/2 as "level" but cos(theta)=0 there
    // makes yaw a no-op on movement (drift-doesn't-follow-heading bug).
    // We rewrite the movement math and the terrain-render camera axes to
    // match this convention so LEFT/RIGHT actually steer the plane.
    theta  = 0.0f;                   // level
    theta1 = 0.0f;                   // facing +y (north)
    theta2 = 0.0f;
    speed  = 2.5f;

    // Terry: x=MAP_WIDTH>>1*COORDINATE_SCALE*MAP_SCALE  etc.
    cam_x = (int64_t)(MAP_WIDTH  >> 1) * COORDINATE_SCALE * MAP_SCALE;
    cam_y = (int64_t)(MAP_HEIGHT >> 1) * COORDINATE_SCALE * MAP_SCALE;
    cam_z = (int64_t)64                * COORDINATE_SCALE * MAP_SCALE;

    game_t0_ms = shrine_ms();
    // Kick off the opening talon swoop so the player immediately reads
    // "you're a bird" — matches Terry's rendering where the talons come
    // in early. Player can re-trigger with B at any time.
    s_talon_swoop_ms = game_t0_ms;
}

// -----------------------------------------------------------------------
// Main loop -- LITERAL port of the switch inside EagleDive() (lines
// 890-964). Terry has ScanKey with CH_ vs SC_ splits; we just map
// arrows to the exact same theta / theta1 / theta2 updates, and A to
// restart (his '\n' branch).
// -----------------------------------------------------------------------
void game_talons_run(void)
{
    Init_EagleDive();
    uint32_t last = shrine_ms();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        // Controls — Terry's original mapping is inverted vs. what most
        // players expect (his DOWN made theta more negative, and given
        // the world convention that means climb, so his DOWN meant
        // "pull back stick = climb", classic flight-sim style). On our
        // d-pad users expect DOWN = dive. Swap the deltas so pressing
        // DOWN pitches the nose down (theta more positive, dz negative)
        // and UP pitches up.
        //   RIGHT: theta1 += CTRLS_SCALE    (yaw right — Terry verbatim)
        //   LEFT:  theta1 -= CTRLS_SCALE    (yaw left  — Terry verbatim)
        // Standard flight-sim convention: DOWN = pitch nose down (dive
        // = decrease theta), UP = pitch nose up (climb = increase
        // theta). LEFT/RIGHT rotate the heading (theta1).
        if (shrine_key_held(BTN_DOWN))  theta  -= CTRLS_SCALE;
        if (shrine_key_held(BTN_UP))    theta  += CTRLS_SCALE;
        if (shrine_key_held(BTN_RIGHT)) theta1 += CTRLS_SCALE;
        if (shrine_key_held(BTN_LEFT))  theta1 -= CTRLS_SCALE;
        // Clamp pitch so player can't loop upside-down.
        if (theta >  1.4f) theta =  1.4f;
        if (theta < -1.4f) theta = -1.4f;

        // Terry: theta1=Wrap(theta1); theta=Wrap(theta); theta2=Wrap(theta2);
        theta1 = Wrap(theta1);
        theta  = Wrap(theta);
        theta2 = Wrap(theta2);

        // Terry: '\n' branch cleans up and re-inits.
        if (shrine_key_pressed(BTN_A)) {
            Init_EagleDive();
            last = shrine_ms();
            continue;
        }

        // B swoops the talons in — homage cue that this is a first-
        // person eagle POV, not a plane. Ignored if a swoop is already
        // playing so the animation doesn't stutter.
        if (shrine_key_pressed(BTN_B) && s_talon_swoop_ms == 0) {
            s_talon_swoop_ms = shrine_ms();
            shrine_beep(900, 40);
        }

        // Terry's AnimateTask advances every ANIMATE_MS=10.
        uint32_t now = shrine_ms();
        float mS = (float)(now - last);
        if (mS > 80.0f) mS = 80.0f;
        last = now;
        AnimateStep(mS);
        CheckGameOver();

        // Terry's BSPEagleDive selects background color: BLACK underwater
        // (dead), BLUE if underwater but on water, LTCYAN in air.
        int gx, gy, h_over;
        cam_grid(&gx, &gy, &h_over);
        uint16_t bk = C(C_LTCYAN);
        if (h_over < 0) {
            if (elevations[gy][gx] > WATER_ELEVATION * MAP_SCALE) bk = C(C_BLACK);
            else                                                  bk = C(C_BLUE);
        }
        // Horizon moves DOWN when climbing (theta > 0) and UP when
        // diving (theta < 0) — standard flight-sim: you see more sky
        // when nosed up, more ground when nosed down.
        int horizon = SCREEN_H / 2 + (int)(theta * 120.0f);
        if (horizon < 0) horizon = 0;
        if (horizon > SCREEN_H) horizon = SCREEN_H;

        // Terry: "if (height>=0 && !(-pi/4<=Wrap(theta-pi)<pi/4)) draw scene"
        // i.e. only render terrain when not looking straight up.
        bool draw_scene = (h_over >= 0);
        if (draw_scene) {
            // Sky above horizon (Terry's LTCYAN), haze below (LTGREEN).
            for (int y = 0; y < horizon; y++)
                fb_hline(0, y, SCREEN_W, C(C_LTCYAN));
            for (int y = horizon; y < SCREEN_H; y++)
                fb_hline(0, y, SCREEN_W, C(C_LTGREEN));
            RenderTerrain(horizon);
            RenderFish(horizon);
        } else {
            // Below terrain: solid backdrop (BLACK for dead, BLUE for water)
            for (int y = 0; y < SCREEN_H; y++)
                fb_hline(0, y, SCREEN_W, bk);
        }
        RenderHUD();
        display_present_full(s_fb);
    }
}
