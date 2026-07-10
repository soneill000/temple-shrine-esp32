// audio.c — LEDC PWM piezo + background song task.

#include "audio.h"
#include "hw.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"

#define TAIL_SILENCE_MS 25

static SemaphoreHandle_t s_ledc_mtx;
static volatile bool     s_muted;
static TaskHandle_t      s_song_task;

// Song state (guarded by mutex during swap).
static const note_t *volatile s_song;
static volatile int           s_song_n;
static volatile bool          s_song_loop;
static volatile bool          s_song_on;

static void tone_on(int freq_hz)
{
    if (freq_hz < 20 || s_muted) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        return;
    }
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq_hz);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void tone_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void song_task(void *arg)
{
    (void)arg;
    int i = 0;
    while (1) {
        if (!s_song_on || !s_song || s_song_n == 0) {
            tone_off();
            vTaskDelay(pdMS_TO_TICKS(50));
            i = 0;
            continue;
        }
        if (i >= s_song_n) {
            if (s_song_loop) { i = 0; }
            else { s_song_on = false; continue; }
        }
        const note_t *n = &s_song[i++];
        int hold = n->ms > TAIL_SILENCE_MS ? n->ms - TAIL_SILENCE_MS : n->ms;
        xSemaphoreTake(s_ledc_mtx, portMAX_DELAY);
        tone_on(n->hz);
        xSemaphoreGive(s_ledc_mtx);
        vTaskDelay(pdMS_TO_TICKS(hold));
        xSemaphoreTake(s_ledc_mtx, portMAX_DELAY);
        tone_off();
        xSemaphoreGive(s_ledc_mtx);
        vTaskDelay(pdMS_TO_TICKS(TAIL_SILENCE_MS));
    }
}

void audio_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 4000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);

    ledc_channel_config_t c = {
        .gpio_num   = PIN_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&c);

    s_ledc_mtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(song_task, "song", 3072, NULL, 4, &s_song_task, 0);
}

void audio_beep(int freq_hz, int ms)
{
    if (s_muted) { vTaskDelay(pdMS_TO_TICKS(ms)); return; }
    xSemaphoreTake(s_ledc_mtx, portMAX_DELAY);
    tone_on(freq_hz);
    xSemaphoreGive(s_ledc_mtx);
    vTaskDelay(pdMS_TO_TICKS(ms));
    xSemaphoreTake(s_ledc_mtx, portMAX_DELAY);
    tone_off();
    xSemaphoreGive(s_ledc_mtx);
}

void audio_off(void)
{
    xSemaphoreTake(s_ledc_mtx, portMAX_DELAY);
    tone_off();
    xSemaphoreGive(s_ledc_mtx);
}

void audio_play_song(const note_t *notes, int count, bool loop)
{
    s_song_on = false;
    vTaskDelay(pdMS_TO_TICKS(10));   // let the task notice
    s_song = notes;
    s_song_n = count;
    s_song_loop = loop;
    s_song_on = true;
}

void audio_stop_song(void) { s_song_on = false; }
bool audio_song_playing(void) { return s_song_on; }

void audio_mute_toggle(void) { s_muted = !s_muted; }
bool audio_is_muted(void)    { return s_muted; }
