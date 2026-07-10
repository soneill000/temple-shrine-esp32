// audio_sdl.c — silent stub of the audio backend for the desktop harness.
//
// The harness's main job is graphics iteration; real square-wave synthesis
// via SDL_QueueAudio is a nice-to-have that we can add later. For now beeps
// and songs are printed to stderr so you can still see what's happening
// without actually making noise.

#include "audio.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>

static bool s_muted;

void audio_init(void) {}
void audio_off(void)  {}

void audio_beep(int freq_hz, int ms)
{
    if (s_muted) { SDL_Delay(ms); return; }
    fprintf(stderr, "[beep %d hz %d ms]\n", freq_hz, ms);
    SDL_Delay(ms);
}

void audio_play_song(const note_t *notes, int count, bool loop)
{
    (void)notes; (void)loop;
    fprintf(stderr, "[song start, %d notes]\n", count);
}

void audio_stop_song(void)      { fprintf(stderr, "[song stop]\n"); }
bool audio_song_playing(void)   { return false; }
void audio_mute_toggle(void)    { s_muted = !s_muted; }
bool audio_is_muted(void)       { return s_muted; }
