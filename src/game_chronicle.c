// game_chronicle.c — a multi-page DolDoc-style reader with respectful,
// public-domain material about Terry, TempleOS, and this shrine.
//
// All facts drawn from public interviews, streams, and the TempleOS wiki.
// LEFT/RIGHT turns pages, BOOT exits.

#include "games.h"
#include "shrine.h"
#include "hw.h"
#include "palette.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *title;
    const char *lines[24];   // up to 24 body rows, terminate with NULL
} page_t;

static const page_t PAGES[] = {
    // --- Page 1: Cover ---
    { "* THE CHRONICLE *", {
        "",
        "",
        "",
        "     T H E   C H R O N I C L E",
        "",
        "                 OF",
        "",
        "         T E M P L E   O S",
        "",
        "",
        "",
        "        A REMEMBRANCE",
        "",
        "",
        "     LEFT / RIGHT   TURN PAGE",
        "     BOOT           EXIT",
        NULL,
    }},
    // --- Page 2: Terry ---
    { "TERRY A. DAVIS", {
        "",
        "     TERRY  A.  DAVIS",
        "     1 9 6 9   -   2 0 1 8",
        "",
        "",
        "  BORN IN WEST ALLIS,",
        "  WISCONSIN. STUDIED",
        "  ELECTRICAL ENGINEERING",
        "  AT ARIZONA STATE.",
        "",
        "  HE SPENT TWO DECADES",
        "  BUILDING WHAT HE CALLED",
        "  THE THIRD TEMPLE.",
        "",
        "  HE BUILT IT BECAUSE HE",
        "  WANTED TO -- AND BECAUSE",
        "  HE FELT GOD HAD ASKED",
        "  HIM TO.",
        "",
        "  HE DIED IN AUGUST 2018.",
        "  HE WAS 48.",
        NULL,
    }},
    // --- Page 3: The OS ---
    { "TEMPLE  OPERATING  SYSTEM", {
        "",
        "  A 64-BIT RING-0",
        "  OPERATING SYSTEM.",
        "",
        "     640 X 480 SCREEN",
        "     16 COLORS ALWAYS",
        "     PC SPEAKER MONO",
        "     NO NETWORKING",
        "     NO SECURITY",
        "",
        "  ALL FILES ARE DOLDOC:",
        "  TEXT WITH EMBEDDED",
        "  IMAGES AND CODE.",
        "",
        "  ABOUT 120,000 LINES",
        "  OF SOURCE, ENTIRELY",
        "  SELF-HOSTED.",
        "",
        "  RELEASED AS TOTAL",
        "  PUBLIC DOMAIN.",
        NULL,
    }},
    // --- Page 4: HolyC ---
    { "H O L Y C", {
        "",
        "  TERRYS OWN LANGUAGE.",
        "  A DIALECT OF C.",
        "",
        "  * JIT COMPILATION",
        "  * COMPILE-TIME EXECUTE",
        "      #EXE { ... }",
        "  * NO DEFAULT INT",
        "  * SUB-INT TYPES",
        "  * DEFAULT ARGUMENTS",
        "  * FOO; MEANS FOO()",
        "",
        "  THE COMPILER IS",
        "  WRITTEN IN HOLYC.",
        "",
        "  THE OS IS WRITTEN",
        "  IN HOLYC.",
        "",
        "  IT IS TURTLES",
        "  ALL THE WAY DOWN.",
        NULL,
    }},
    // --- Page 5: The Third Temple ---
    { "THE  THIRD  TEMPLE", {
        "",
        "  IN 1 KINGS 6, THE LORD",
        "  SPECIFIED THE MEASURE-",
        "  MENTS OF SOLOMONS",
        "  TEMPLE.",
        "",
        "  TERRY BELIEVED HE HAD",
        "  BEEN CALLED TO BUILD",
        "  A THIRD TEMPLE.",
        "",
        "  NOT IN STONE.",
        "  IN SOFTWARE.",
        "",
        "  MEASUREMENTS ENCODED",
        "  IN DIVINE INSTRUCTION.",
        "",
        "  HE SPOKE WITH GOD",
        "  THROUGH RANDOMNESS.",
        "",
        "  (SEE:  ORACLE)",
        NULL,
    }},
    // --- Pages 6-8: On Reality (full Terry bird-and-monitor quote,
    // split across three pages for readability. Quote text is verbatim
    // from Terry's vlog; page breaks land on natural clause boundaries.
    // Same title on all three pages so it reads as one continuous piece
    // as the user pages through.)
    { "O N   R E A L I T Y", {
        "",
        "  \"WHAT'S REALITY?",
        "  I DON'T KNOW.",
        "",
        "  WHEN MY BIRD WAS",
        "  LOOKING AT MY",
        "  COMPUTER MONITOR",
        "  I THOUGHT:",
        "",
        "  'THAT BIRD HAS NO IDEA",
        "  WHAT HE'S LOOKING AT.'",
        NULL,
    }},
    { "O N   R E A L I T Y", {
        "",
        "  AND YET WHAT DOES THE",
        "  BIRD DO?  DOES HE PANIC?",
        "",
        "  NO -- HE CAN'T REALLY",
        "  PANIC.  HE JUST DOES THE",
        "  BEST HE CAN.",
        "",
        "  IS HE ABLE TO LIVE IN A",
        "  WORLD WHERE HE'S SO",
        "  IGNORANT?  WELL, HE",
        "  DOESN'T REALLY HAVE",
        "  A CHOICE.",
        "",
        "  YEAH, HE CAN KINDA LIVE.",
        "  USUALLY THE BIRD IS OK",
        "  EVEN THOUGH HE DOESN'T",
        "  UNDERSTAND THE WORLD.",
        "",
        "  HE CAN KINDA LEARN",
        "  WHAT'S SAFE AND WHAT'S",
        "  DANGEROUS.",
        NULL,
    }},
    { "O N   R E A L I T Y", {
        "",
        "  THAT'S WHERE I'VE",
        "  BEEN LIVING.",
        "",
        "  YOU'RE THAT BIRD LOOKING",
        "  AT THE MONITOR, AND",
        "  YOU'RE THINKING TO",
        "  YOURSELF:",
        "",
        "  'I CAN FIGURE THIS OUT.'",
        "",
        "  MAYBE YOU HAVE SOME",
        "  BIRD IDEAS.",
        "",
        "  MAYBE THAT'S THE BEST",
        "  YOU CAN DO.\"",
        "",
        "",
        "         -- TERRY DAVIS",
        NULL,
    }},
    // --- Page 9: This Shrine ---
    { "T H I S   S H R I N E", {
        "",
        "  BUILT FOR THE",
        "  2026 DEF CON BADGE",
        "  BY SARAH ONEILL",
        "  WITH CLAUDE 4.7.",
        "",
        "  SCENES INCLUDE PORTS",
        "  OF TERRYS OWN",
        "  SOURCE:  AFTER EGYPT,",
        "  BUGBIRD, BOMBERGOLF,",
        "  SQUIRT, EAGLE DIVE.",
        "",
        "  16-COLOR VGA PALETTE.",
        "  8X8 CGA-STYLE FONT.",
        "  MONOPHONIC PIEZO.",
        "  KJV VOCAB.  REAL",
        "  MESHTASTIC RADIO.",
        "",
        "  IN MEMORY OF TERRY.",
        "  MAY HE REST IN PEACE.",
        NULL,
    }},
};
#define N_PAGES (int)(sizeof(PAGES)/sizeof(PAGES[0]))

static int s_page;

static void draw_chrome(void)
{
    shrine_clear(C_BG);
    shrine_window(0, 0, TEXT_COLS, TEXT_ROWS, C_YELLOW, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, 3, G_HLINE[0], C_YELLOW, C_BG);
    for (int c = 1; c < TEXT_COLS - 1; c++)
        shrine_putc(c, TEXT_ROWS - 3, G_HLINE[0], C_YELLOW, C_BG);
}

static void draw_page(void)
{
    // Clear the body region only.
    shrine_fill_rect(GLYPH_W, 4 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W,
                     (TEXT_ROWS - 7) * GLYPH_H,
                     PAL_RGB565[C_BG]);
    // Clear header row.
    shrine_fill_rect(GLYPH_W, 1 * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, GLYPH_H,
                     PAL_RGB565[C_BG]);

    // Title.
    shrine_puts_centered(1, PAGES[s_page].title, C_YELLOW, C_BG);

    // Body lines.
    for (int i = 0; i < 24; i++) {
        const char *line = PAGES[s_page].lines[i];
        if (!line) break;
        color_t c = C_WHITE;
        // Colorize by content cues.
        if (line[0] && line[0] != ' ') c = C_LTCYAN;
        if (strstr(line, "* ")) c = C_LTGREEN;
        if (strstr(line, " - ")) c = C_LTGRAY;
        shrine_puts(2, 4 + i, line, c, C_BG);
    }

    // Footer: page X of Y + hints.
    char footer[48];
    snprintf(footer, sizeof(footer), "PAGE  %d / %d", s_page + 1, N_PAGES);
    shrine_fill_rect(GLYPH_W, (TEXT_ROWS - 2) * GLYPH_H,
                     (TEXT_COLS - 2) * GLYPH_W, GLYPH_H,
                     PAL_RGB565[C_BG]);
    shrine_puts_centered(TEXT_ROWS - 2, footer, C_LTGREEN, C_BG);
}

void game_chronicle_run(void)
{
    s_page = 0;
    draw_chrome();
    draw_page();

    while (1) {
        shrine_input_scan();
        if (shrine_should_quit()) return;

        if (shrine_key_pressed(BTN_RIGHT) || shrine_key_pressed(BTN_A)) {
            s_page = (s_page + 1) % N_PAGES;
            draw_page();
            shrine_beep(1600, 25);
        }
        if (shrine_key_pressed(BTN_LEFT)) {
            s_page = (s_page - 1 + N_PAGES) % N_PAGES;
            draw_page();
            shrine_beep(1200, 25);
        }
        shrine_sleep_ms(30);
    }
}
