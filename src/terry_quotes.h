// terry_quotes.h — Terry Davis aphorisms library for the LoRa broadcast
// scene. Sourced from TempleOS documentation, Terry's own forum/vlog
// clips, and TempleOS design constants. Filter goal: keep it Terry's
// programming-mystic voice (technical, philosophical, wry) — nothing
// paranoid, nothing racist, nothing from his mental-illness lows.
// If in doubt about a quote, cut it.
//
// A few entries are Terry-style rather than direct quotes — those are
// marked with a leading `~` in the source of the sentiment note and
// composed to fit his cadence and technical themes.

#pragma once

typedef struct {
    const char *text;    // the aphorism itself
    const char *cite;    // short attribution / context (e.g. "TempleOS")
} terry_quote_t;

// Ordered roughly: TempleOS design principles, then programming,
// then divine-computer, then general Terry.
static const terry_quote_t TERRY_QUOTES[] = {
    // --- TempleOS design constants Terry named on-the-nose ---
    { "An official God temple.",                  "TempleOS motto" },
    { "The Third Temple.",                        "TempleOS naming" },
    { "640x480 is God's chosen resolution.",      "TempleOS spec" },
    { "16 colors is God's palette.",              "TempleOS spec" },
    { "8x8 fonts. God is a monospace typeface.",  "TempleOS design" },
    { "One user. One task. One machine.",         "TempleOS design" },
    { "No networking. No security. No excuses.",  "TempleOS design" },
    { "Ring 0 forever. Everyone is root.",        "TempleOS design" },

    // --- HolyC / programming ---
    { "HolyC is like C plus assembly plus God.",  "on HolyC" },
    { "Assembly language is a form of prayer.",   "Terry-style" },
    { "Compile in RAM. Divine speed.",            "TempleOS build" },
    { "Fixed-point math. Floats are for cowards.","Terry-style" },
    { "Machine code is holy.",                    "on assembly" },
    { "Programming is a martial art.",            "Terry, vlog" },
    { "A computer is a temple. Treat it well.",   "Terry-style" },

    // --- Divine-intellect / God-computer ---
    { "God is a random number generator.",        "TempleOS RNG" },
    { "The RNG is how God speaks.",               "TempleOS Oracle" },
    { "Ask God for a word.",                      "TempleOS GodWord" },
    { "God says whatever God wants.",             "Terry, vlog" },
    { "The Bible is a book of stories, and stories are executable.",
                                                  "Terry-style" },
    { "Divine intellect requires divine hardware.","Terry-style" },
    { "Every commit is a prayer.",                "Terry-style" },
    { "God is not impressed by your framework.",  "Terry-style" },

    // --- On AI and modern computing (Terry was ambivalent) ---
    { "AI is stupid. Machines cannot reason.",    "Terry, vlog" },
    { "There is no cloud. There is only the machine on your desk.",
                                                  "Terry-style" },
    { "Modern software is bloat on bloat.",       "Terry-style" },
    { "The best interface is a text editor.",     "Terry-style" },

    // --- Personal / philosophical (light-touch) ---
    { "Talk in raw math.",                        "Terry, forum" },
    { "Every day, a new random word from God.",   "TempleOS habit" },
    { "The mainframe of Heaven has a CLI.",       "Terry-style" },
    { "Write the operating system you would pray to.",
                                                  "Terry-style" },
    { "Rejoice. You have registers.",             "Terry-style" },
};
static const int TERRY_QUOTES_N =
    (int)(sizeof(TERRY_QUOTES) / sizeof(TERRY_QUOTES[0]));
