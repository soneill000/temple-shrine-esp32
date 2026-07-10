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

#include <stdio.h>
#include <string.h>
#include <math.h>

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

// --- Drawing ---

static void draw_faucet(void)
{
    // Pipe rising out of the top left of the ground plane, up to the faucet.
    shrine_fill_rect(FAUCET_X - 3, FAUCET_Y, 6, GROUND_Y - FAUCET_Y, C_DKGRAY);
    shrine_rect    (FAUCET_X - 3, FAUCET_Y, 6, GROUND_Y - FAUCET_Y, C_BLACK);
    // Faucet head.
    shrine_fill_circle(FAUCET_X, FAUCET_Y, 5, C_LTGRAY);
    shrine_circle    (FAUCET_X, FAUCET_Y, 5, C_BLACK);
}

static void draw_ground(void)
{
    shrine_hline(0, GROUND_Y, SCREEN_W, C_YELLOW);
    shrine_fill_rect(0, GROUND_Y + 1, SCREEN_W, SCREEN_H - GROUND_Y - 1, C_BROWN);
}

static void draw_hose(void)
{
    // Thick hose: filled circles at each mass plus lines between them.
    for (int i = 0; i < N_HOSE - 1; i++) {
        int x1 = (int)s_hose[i].x,   y1 = (int)s_hose[i].y;
        int x2 = (int)s_hose[i+1].x, y2 = (int)s_hose[i+1].y;
        // Draw three parallel lines for a thick hose.
        shrine_line(x1, y1, x2, y2, C_GREEN);
        shrine_line(x1, y1 - 1, x2, y2 - 1, C_GREEN);
        shrine_line(x1, y1 + 1, x2, y2 + 1, C_GREEN);
    }
    // Nozzle head.
    shrine_fill_circle((int)s_nozzle_x, (int)s_nozzle_y, 4, C_LTGREEN);
    shrine_circle    ((int)s_nozzle_x, (int)s_nozzle_y, 4, C_BLACK);
}

static void draw_drops(void)
{
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drops[i].active) continue;
        int x = (int)s_drops[i].x, y = (int)s_drops[i].y;
        shrine_pixel(x,     y,     C_LTCYAN);
        shrine_pixel(x + 1, y,     C_LTCYAN);
        shrine_pixel(x,     y + 1, C_WHITE);
    }
}

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_puts_centered(1, "*  SQUIRT  *", C_YELLOW, C_BG);
    shrine_puts_centered(2, "AIM THE HOSE",  C_LTCYAN, C_BG);
    shrine_puts_centered(TEXT_ROWS - 1,
                         "ARROWS AIM   A JET   BOOT EXIT",
                         C_LTGRAY, C_BG);
}

static void draw_playfield(void)
{
    // Clear the play area (below the header, above the hint).
    shrine_fill_rect(0, 24, SCREEN_W, SCREEN_H - 32, PAL_RGB565[C_BG]);
    draw_ground();
    draw_faucet();
    draw_hose();
    draw_drops();
}

void game_squirt_run(void)
{
    reset();
    draw_chrome();
    draw_playfield();

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
