// audio.h — piezo (GPIO 9) LEDC PWM. TempleOS-style: mono square wave.
// TempleOS games use Beep() and Snd(ona). This exposes the same idea.

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct { uint16_t hz; uint16_t ms; } note_t;

void audio_init(void);

// Fire-and-forget beep (blocks caller for `ms`).
void audio_beep(int freq_hz, int ms);

// Silence.
void audio_off(void);

// Play a note sequence in a background task. Loops until audio_stop_song().
// Only one song plays at a time — starting a new one replaces the current.
void audio_play_song(const note_t *notes, int count, bool loop);
void audio_stop_song(void);
bool audio_song_playing(void);

// Persistent mute — survives songs, blocks all output.
void audio_mute_toggle(void);
bool audio_is_muted(void);
