// audio_sdl.c — SDL2 audio backend for the desktop harness.
//
// Simple monophonic square-wave synthesizer to match the badge piezo:
// one tone at a time, driven by the "note queue" the song thread walks
// through, plus a fire-and-forget audio_beep for SFX. Sample rate 22050,
// mono s16.

#include "audio.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#define SR         22050
#define TAIL_MS    25

static SDL_AudioDeviceID s_dev;

// Current tone state (guarded by audio device lock during callback).
static volatile int  s_freq;    // 0 = silent
static volatile int  s_phase;   // sample count into current period
static volatile bool s_muted;

// Song thread state.
static SDL_Thread   *s_song_thread;
static const note_t *volatile s_song_notes;
static volatile int  s_song_n;
static volatile bool s_song_loop;
static volatile bool s_song_on;

static void audio_cb(void *userdata, Uint8 *stream, int len_bytes)
{
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int nsamp = len_bytes / 2;
    int f = s_freq;
    if (f == 0 || s_muted) {
        memset(stream, 0, len_bytes);
        return;
    }
    int period = SR / f;
    if (period < 2) period = 2;
    int half   = period / 2;
    int ph = s_phase;
    for (int i = 0; i < nsamp; i++) {
        out[i] = (ph < half) ? +6000 : -6000;
        ph = (ph + 1) % period;
    }
    s_phase = ph;
}

static void set_tone(int hz)
{
    SDL_LockAudioDevice(s_dev);
    s_freq  = hz;
    s_phase = 0;
    SDL_UnlockAudioDevice(s_dev);
}

// Song thread — walks through the note array with SDL_Delay between notes.
static int song_thread_fn(void *arg)
{
    (void)arg;
    int i = 0;
    while (1) {
        if (!s_song_on || !s_song_notes || s_song_n == 0) {
            set_tone(0);
            SDL_Delay(50);
            i = 0;
            continue;
        }
        if (i >= s_song_n) {
            if (s_song_loop) { i = 0; }
            else { s_song_on = false; continue; }
        }
        const note_t *n = &s_song_notes[i++];
        int hold = n->ms > TAIL_MS ? n->ms - TAIL_MS : n->ms;
        set_tone(n->hz);
        SDL_Delay(hold);
        set_tone(0);
        SDL_Delay(TAIL_MS);
    }
    return 0;
}

void audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want = {
        .freq = SR, .format = AUDIO_S16SYS, .channels = 1,
        .samples = 512, .callback = audio_cb,
    };
    SDL_AudioSpec have;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!s_dev) {
        fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    s_song_thread = SDL_CreateThread(song_thread_fn, "song", NULL);
}

void audio_off(void) { set_tone(0); }

void audio_beep(int freq_hz, int ms)
{
    if (s_muted) { SDL_Delay(ms); return; }
    set_tone(freq_hz);
    SDL_Delay(ms);
    set_tone(0);
}

void audio_play_song(const note_t *notes, int count, bool loop)
{
    s_song_on   = false;
    SDL_Delay(10);
    s_song_notes = notes;
    s_song_n     = count;
    s_song_loop  = loop;
    s_song_on    = true;
}

void audio_stop_song(void)      { s_song_on = false; }
bool audio_song_playing(void)   { return s_song_on; }
void audio_mute_toggle(void)    { s_muted = !s_muted; }
bool audio_is_muted(void)       { return s_muted; }
