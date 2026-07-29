// hymns.h — public-domain hymn melodies for the piezo.
// All tunes are 18th–19th century or earlier — well out of copyright.
// Simplified transcriptions; monophonic for the piezo.

#pragma once

#include "audio.h"

// Note frequencies (equal temperament, A4 = 440).
#define N_E3  165
#define N_G3  196
#define N_A3  220
#define N_B3  247
#define N_C4  262
#define N_D4  294
#define N_E4  330
#define N_F4  349
#define N_G4  392
#define N_A4  440
#define N_B4  494
#define N_C5  523
#define N_D5  587
#define N_E5  659
#define N_F5  698
#define N_G5  784

#define Q_    500   // quarter (120 BPM)
#define E_    250
#define S_    125   // sixteenth
#define QD_   750
#define H_    1000
#define HD_   1500

// "Old Hundredth" (1551) / Doxology — one full verse.
static const note_t HYMN_OLD_HUNDREDTH[] = {
    {N_C5, Q_}, {N_C5, Q_}, {N_B4, Q_}, {N_A4, Q_},
    {N_G4, Q_}, {N_A4, Q_}, {N_B4, Q_}, {N_C5, H_},
    {N_C5, Q_}, {N_B4, Q_}, {N_A4, Q_}, {N_G4, Q_},
    {N_F4, Q_}, {N_G4, Q_}, {N_A4, Q_}, {N_G4, H_},
    {N_G4, Q_}, {N_A4, Q_}, {N_B4, Q_}, {N_C5, Q_},
    {N_D5, Q_}, {N_C5, Q_}, {N_B4, Q_}, {N_A4, H_},
    {N_A4, Q_}, {N_G4, Q_}, {N_F4, Q_}, {N_E4, Q_},
    {N_D4, Q_}, {N_E4, Q_}, {N_F4, Q_}, {N_G4, HD_},
    {0, H_},
};

// "Amazing Grace" (1779, tune "New Britain" 1829) — opening verse in C.
static const note_t HYMN_AMAZING_GRACE[] = {
    {N_G4, QD_}, {N_C5, E_},
    {N_C5, Q_}, {N_E5, Q_}, {N_C5, Q_}, {N_E5, Q_},
    {N_D5, Q_},
    {N_C5, QD_}, {N_A4, E_},
    {N_G4, H_},
    {N_G4, QD_}, {N_C5, E_},
    {N_C5, Q_}, {N_E5, Q_}, {N_C5, Q_}, {N_E5, Q_},
    {N_D5, HD_},
    {N_E5, QD_}, {N_D5, E_},
    {N_C5, Q_}, {N_A4, Q_}, {N_A4, Q_}, {N_G4, Q_},
    {N_A4, HD_},
    {N_C5, Q_}, {N_A4, Q_}, {N_G4, Q_},
    {N_C5, HD_},
    {0, H_},
};

// "Nearer, My God, To Thee" (1856, tune "Bethany") — first phrase.
static const note_t HYMN_NEARER_MY_GOD[] = {
    {N_C4, Q_}, {N_C4, Q_}, {N_E4, Q_}, {N_C4, Q_},
    {N_F4, Q_}, {N_E4, H_},
    {N_C4, Q_}, {N_C4, Q_}, {N_E4, Q_}, {N_C4, Q_},
    {N_G4, Q_}, {N_A4, H_},
    {N_C5, Q_}, {N_A4, Q_}, {N_G4, Q_}, {N_E4, Q_},
    {N_F4, Q_}, {N_G4, HD_},
    {0, H_},
};

// "Holy Holy Holy" (1826, tune "Nicaea" by John Dykes 1861).
static const note_t HYMN_HOLY_HOLY[] = {
    {N_D4, Q_}, {N_D4, Q_}, {N_A4, Q_}, {N_A4, Q_},
    {N_B4, Q_}, {N_C5, Q_}, {N_A4, H_},
    {N_G4, Q_}, {N_G4, Q_}, {N_F4, Q_}, {N_E4, Q_},
    {N_D4, HD_},
    {N_D4, Q_}, {N_D4, Q_}, {N_A4, Q_}, {N_A4, Q_},
    {N_C5, Q_}, {N_C5, Q_}, {N_B4, H_},
    {N_A4, Q_}, {N_G4, Q_}, {N_F4, Q_}, {N_G4, Q_},
    {N_A4, HD_},
    {0, H_},
};

// "Rock of Ages" (1763, tune "Toplady" by Thomas Hastings 1830).
static const note_t HYMN_ROCK_OF_AGES[] = {
    {N_G4, Q_}, {N_G4, Q_}, {N_G4, Q_}, {N_A4, Q_},
    {N_B4, H_},
    {N_A4, Q_}, {N_G4, Q_}, {N_A4, Q_}, {N_B4, Q_},
    {N_C5, HD_},
    {N_B4, Q_}, {N_A4, Q_}, {N_G4, Q_}, {N_A4, Q_},
    {N_B4, H_},
    {N_A4, Q_}, {N_G4, Q_}, {N_F4, Q_}, {N_E4, Q_},
    {N_D4, HD_},
    {0, H_},
};

// "Come Thou Fount" (1758, tune "Nettleton" 1813).
static const note_t HYMN_COME_THOU_FOUNT[] = {
    {N_C5, Q_}, {N_A4, Q_}, {N_G4, Q_}, {N_A4, Q_},
    {N_G4, Q_}, {N_F4, Q_}, {N_E4, H_},
    {N_G4, Q_}, {N_F4, Q_}, {N_E4, Q_}, {N_F4, Q_},
    {N_G4, HD_},
    {N_C5, Q_}, {N_A4, Q_}, {N_G4, Q_}, {N_A4, Q_},
    {N_G4, Q_}, {N_F4, Q_}, {N_E4, H_},
    {N_D4, Q_}, {N_E4, Q_}, {N_G4, Q_}, {N_G4, Q_},
    {N_C4, HD_},
    {0, H_},
};

// "How Great Thou Art" (1885, Swedish tune "O Store Gud") — opening.
static const note_t HYMN_HOW_GREAT[] = {
    {N_G4, Q_}, {N_G4, E_}, {N_A4, E_}, {N_G4, Q_}, {N_E4, Q_},
    {N_G4, H_},
    {N_F4, Q_}, {N_A4, Q_}, {N_G4, Q_}, {N_F4, Q_},
    {N_E4, HD_},
    {N_E4, Q_}, {N_E4, E_}, {N_F4, E_}, {N_E4, Q_}, {N_D4, Q_},
    {N_E4, H_},
    {N_D4, Q_}, {N_G4, Q_}, {N_F4, Q_}, {N_E4, Q_},
    {N_D4, HD_},
    {0, H_},
};

// "Paranoid" (Black Sabbath, 1970) — Terry famously played this on his
// TempleOS piano. Iommi's main riff transcribed for monophonic piezo:
// the E-power-chord chug voiced as the root note, followed by the
// climbing E-G-A hook and the descending run into the verse. Not a
// hymn in the churchly sense — Terry's own worship music.
static const note_t HYMN_PARANOID[] = {
    // Opening: fast E-E-E-E chug that everyone recognises.
    {N_E4, E_}, {N_E4, E_}, {N_E4, E_}, {N_E4, E_},
    {N_E4, E_}, {N_E4, E_}, {N_E4, E_}, {N_E4, E_},
    // Iommi's climbing hook: E - E - G - A repeated.
    {N_E4, E_}, {N_E4, E_}, {N_G4, E_}, {N_A4, E_},
    {N_E4, E_}, {N_E4, E_}, {N_G4, E_}, {N_A4, E_},
    // Turnaround: A down to G down to E.
    {N_A4, Q_}, {N_G4, Q_}, {N_E4, H_},
    // Verse riff: E-D descending pull-off pattern.
    {N_E4, E_}, {N_D4, E_}, {N_E4, E_}, {N_D4, E_},
    {N_E4, E_}, {N_D4, E_}, {N_E4, Q_},
    // Second round of the hook so the riff is recognisable as a loop.
    {N_E4, E_}, {N_E4, E_}, {N_G4, E_}, {N_A4, E_},
    {N_E4, E_}, {N_E4, E_}, {N_G4, E_}, {N_A4, E_},
    {N_A4, Q_}, {N_G4, Q_}, {N_E4, HD_},
    {0, H_},
};

// "Enter Sandman" (Metallica, 1991) — main riff. Terry's TempleOS jukebox
// era overlapped with metal-radio staples; this reads as a companion
// track to Paranoid. Iconic Hammett riff simplified to the root melodic
// line.
static const note_t HYMN_ENTER_SANDMAN[] = {
    // Opening clean-guitar riff (E minor).
    {N_E4, Q_}, {N_G4, Q_}, {N_E4, Q_},
    {N_E4, E_}, {N_D4, E_}, {N_E4, Q_},
    {N_E4, Q_}, {N_G4, Q_}, {N_E4, Q_},
    {N_B3, Q_},
    // Second phrase.
    {N_E4, Q_}, {N_G4, Q_}, {N_E4, Q_},
    {N_E4, E_}, {N_D4, E_}, {N_E4, Q_},
    {N_D4, Q_}, {N_E4, Q_}, {N_G4, Q_},
    {N_E4, HD_},
    {0, H_},
};
