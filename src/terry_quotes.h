// terry_quotes.h — Terry Davis verbatim quotes library.
//
// This is a user-curated set. Order matches PHRASES.md exactly. Every
// quote is verbatim from a verifiable source. Do NOT expand this list
// without user review — trims happen in PHRASES.md and get mirrored here.

#pragma once

typedef struct {
    const char *text;    // the quote itself, unedited
    const char *cite;    // short attribution
} terry_quote_t;

static const terry_quote_t TERRY_QUOTES[] = {
    // Bird-and-monitor quote pinned first per user request.
    { "What's reality? I don't know. When my bird was looking at my computer monitor I thought, 'That bird has no idea what he's looking at.'",
                                                     "Terry, vlog" },

    // --- User-supplied verbatim ---
    { "I like elephants and God likes elephants.",    "Terry, vlog" },
    { "The first time you meet an angel you get a horrible beating.",
                                                     "Terry, vlog" },
    { "Brontosaurs' feet hurt when stepped.",         "Terry, vlog" },
    { "Is this too much voodoo?",                     "Terry, vlog" },
    { "This is voodoo; the question is - is this too much.",
                                                     "Terry, vlog" },
    { "Thou shall not litter.",                       "Terry, vlog" },

    // --- Vlog / interview lines (Wikiquote) ---
    { "You banned me from Twitter, God bans you from Heaven.",
                                                     "Terry, vlog" },
    { "God likes music that makes you feel.",         "Terry, vlog" },
    { "I use Ubuntu to download VMware to run TempleOS.",
                                                     "Terry, forum" },
};
static const int TERRY_QUOTES_N =
    (int)(sizeof(TERRY_QUOTES) / sizeof(TERRY_QUOTES[0]));
