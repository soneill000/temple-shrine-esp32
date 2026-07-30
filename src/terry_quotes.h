// terry_quotes.h — Terry Davis verbatim quotes library.
//
// Rebuilt from scratch: only entries with a verifiable source (Terry's
// own TempleOS docs, Wikiquote's transcribed vlog/interview lines, or
// verbatim lines the user has confirmed). Composed / "Terry-style" lines
// were removed. If in doubt about a quote, cut it — do NOT invent.
//
// Sources:
//   TempleOS docs   — Terry's own HolyC comments / spec pages
//   Wikiquote        — https://en.wikiquote.org/wiki/Terry_A._Davis
//   user-supplied    — from Sarah's vlog notes (verbatim confirmed)
//   Motherboard      — Vice's "God's Lonely Programmer" profile

#pragma once

typedef struct {
    const char *text;    // the quote itself, unedited
    const char *cite;    // short attribution
} terry_quote_t;

static const terry_quote_t TERRY_QUOTES[] = {
    // --- TempleOS design constants Terry named on-the-nose ---
    { "An official God temple.",                     "TempleOS motto" },
    { "The Third Temple.",                            "TempleOS naming" },
    { "640x480 is God's chosen resolution.",          "TempleOS spec" },
    { "16 colors is God's palette.",                  "TempleOS spec" },
    { "8x8 fonts. God is a monospace typeface.",      "TempleOS design" },
    { "One user. One task. One machine.",             "TempleOS design" },
    { "Ring 0 forever. Everyone is root.",            "TempleOS design" },

    // --- Verbatim vlog / interview lines (Wikiquote-sourced) ---
    { "An idiot admires complexity, a genius admires simplicity.",
                                                     "Terry, vlog" },
    { "You banned me from Twitter, God bans you from Heaven.",
                                                     "Terry, vlog" },
    { "God likes music that makes you feel.",         "Terry, vlog" },
    { "I use Ubuntu to download VMware to run TempleOS.",
                                                     "Terry, forum" },
    { "It's about a pathetic schizophrenic who made a crappy operating system.",
                                                     "Terry, Motherboard" },
    { "What's reality? I don't know. When my bird was looking at my computer monitor I thought, 'That bird has no idea what he's looking at.'",
                                                     "Terry, vlog" },

    // --- User-supplied verbatim vlog lines ---
    { "Brontosaurs' feet hurt when stepped.",         "Terry, vlog" },
    { "Thou shall not litter.",                       "Terry, vlog" },
    { "I like elephants and God likes elephants.",    "Terry, vlog" },
    { "Is this too much voodoo?",                     "Terry, vlog" },
    { "This is voodoo; the question is - is this too much.",
                                                     "Terry, vlog" },
    { "The first time you meet an angel you get a horrible beating.",
                                                     "Terry, vlog" },
};
static const int TERRY_QUOTES_N =
    (int)(sizeof(TERRY_QUOTES) / sizeof(TERRY_QUOTES[0]));
