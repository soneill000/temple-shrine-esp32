// bible.c — curated verse table with paraphrased text. See bible.h.
//
// Text is original thematic wording in my own words, not the KJV text.
// Each entry kept under ~32 characters so it fits on one text line at
// our scale=2 rendering. Citations are the real references matching
// verses Terry calls out (Clouds -> Exodus 14:19) plus a starter set
// for Oracle to draw from randomly.
//
// To swap in actual KJV text later (which is genuinely public domain,
// 1611): pull each cited verse from Project Gutenberg's Bible-kjv.txt
// and replace the `text` field. Layout code in scene_clouds and
// game_oracle already handles wrapping if the strings grow longer.

#include "bible.h"
#include "shrine.h"

#include <string.h>

const kjv_verse_t KJV_TABLE[] = {
    // Genesis — creation and covenant
    { "GENESIS",     1,  1, "AT THE START, ALL WAS MADE." },
    { "GENESIS",     8, 22, "THE HARVEST WILL RETURN." },
    { "GENESIS",    12,  1, "GO TO A LAND I WILL SHOW YOU." },

    // Exodus — Moses' journey (After Egypt's core source)
    { "EXODUS",      3,  5, "TAKE OFF YOUR SANDALS." },
    { "EXODUS",      3, 14, "THE VOICE SAID: I AM." },
    { "EXODUS",     14, 19, "THE CLOUD STOOD BEHIND THEM." },
    { "EXODUS",     14, 21, "THE SEA OPENED FOR THEM." },
    { "EXODUS",     16, 15, "THEY NAMED IT MANNA." },
    { "EXODUS",     17,  6, "STRIKE THE ROCK." },
    { "EXODUS",     19, 16, "THUNDER FELL ON THE MOUNT." },
    { "EXODUS",     20,  3, "HAVE NO OTHER GODS." },

    // Numbers — the wilderness years
    { "NUMBERS",    11, 31, "QUAIL FELL AROUND THE CAMP." },
    { "NUMBERS",    20, 11, "WATER POURED FROM THE ROCK." },
    { "NUMBERS",    21,  8, "SET A SERPENT ON A POLE." },

    // Deuteronomy — final teachings
    { "DEUTERONOMY", 6,  5, "LOVE WITH YOUR WHOLE HEART." },
    { "DEUTERONOMY", 8,  3, "NOT ONLY BY BREAD." },
    { "DEUTERONOMY",31,  6, "BE STRONG. NOT ALONE." },

    // Joshua & Judges
    { "JOSHUA",      1,  9, "BE OF GOOD COURAGE." },
    { "JUDGES",      6, 12, "A MIGHTY WARRIOR." },

    // Psalms — comfort and praise
    { "PSALMS",     23,  1, "THE LORD KEEPS ME." },
    { "PSALMS",     27,  1, "WHOM SHALL I FEAR." },
    { "PSALMS",     46, 10, "STAND STILL AND KNOW." },
    { "PSALMS",    103, 15, "WE FLOWER AND FADE." },
    { "PSALMS",    121,  1, "I LOOK TO THE HILLS." },

    // Proverbs — practical wisdom
    { "PROVERBS",    3,  5, "TRUST WITH YOUR WHOLE HEART." },
    { "PROVERBS",   16,  9, "THE STEPS ARE GUIDED." },
    { "PROVERBS",   27, 17, "ONE PERSON SHARPENS ANOTHER." },

    // Isaiah — comfort and vision
    { "ISAIAH",     40,  8, "THE WORD ENDURES." },
    { "ISAIAH",     40, 31, "THOSE WHO WAIT ARE RENEWED." },
    { "ISAIAH",     43,  2, "I AM WITH YOU IN WATERS." },

    // Ecclesiastes — the preacher
    { "ECCLESIASTES",3,  1, "A SEASON FOR EACH THING." },
    { "ECCLESIASTES",9, 11, "TIME AND CHANCE FOR ALL." },

    // Selected New Testament
    { "MATTHEW",     5,  9, "BLESSED ARE THE PEACEMAKERS." },
    { "JOHN",        1,  5, "LIGHT SHINES IN THE DARK." },
    { "JOHN",       14, 27, "I GIVE YOU PEACE." },
    { "REVELATION", 21,  4, "ALL TEARS WILL BE WIPED." },
};
const int KJV_TABLE_N = (int)(sizeof(KJV_TABLE) / sizeof(KJV_TABLE[0]));

const kjv_verse_t *kjv_lookup(const char *book, int chapter, int verse)
{
    for (int i = 0; i < KJV_TABLE_N; i++) {
        if (KJV_TABLE[i].chapter == chapter
            && KJV_TABLE[i].verse == verse
            && strcmp(KJV_TABLE[i].book, book) == 0) {
            return &KJV_TABLE[i];
        }
    }
    return NULL;
}

const kjv_verse_t *kjv_random(void)
{
    if (KJV_TABLE_N <= 0) return NULL;
    return &KJV_TABLE[shrine_god(KJV_TABLE_N)];
}
