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
    { "GENESIS",     1,  1,
      "In the beginning God created the heaven and the earth. "
      "And the earth was without form, and void; and darkness "
      "was upon the face of the deep. And the Spirit of God "
      "moved upon the face of the waters. And God said, Let "
      "there be light: and there was light." },
    { "GENESIS",     8, 22, "THE HARVEST WILL RETURN." },
    { "GENESIS",    12,  1, "GO TO A LAND I WILL SHOW YOU." },

    // Exodus — Moses' journey (After Egypt's core source)
    // Terry's Mt Horeb scene cites Exodus 3:1 (Moses tending flocks).
    { "EXODUS",      3,  1,
      "Now Moses kept the flock of Jethro his father "
      "in law, the priest of Midian: and he led the "
      "flock to the backside of the desert, and came "
      "to the mountain of God, even to Horeb." },
    { "EXODUS",      3,  5, "TAKE OFF YOUR SANDALS." },
    { "EXODUS",      3, 14, "THE VOICE SAID: I AM." },
    // Real KJV text for the verses Terry's After Egypt explicitly cites.
    // KJV (1611) is public domain worldwide. Clouds uses 14:19 and Terry's
    // BibleVerse displays through 14:20; store both here concatenated so
    // the scene can render Terry's exact block.
    { "EXODUS",     14, 19,
      "And the angel of God, which went before the camp of Israel, "
      "removed and went behind them; and the pillar of the cloud "
      "went from before their face, and stood behind them: "
      "And it came between the camp of the Egyptians and the camp "
      "of Israel; and it was a cloud and darkness to them, but it "
      "gave light by night to these: so that the one came not near "
      "the other all the night." },
    { "EXODUS",     14, 21, "THE SEA OPENED FOR THEM." },
    { "EXODUS",     16, 15, "THEY NAMED IT MANNA." },
    // Terry's View Map cites Exodus 16:35 verbatim through BibleVerse().
    // Real KJV text follows so the verse renders identically to his.
    { "EXODUS",     16, 35,
      "And the children of Israel did eat manna forty years, "
      "until they came to a land inhabited; they did eat manna, "
      "until they came unto the borders of the land of Canaan." },
    // Terry's Water Rock cites Exodus 17:6 via BibleVerse(); real KJV.
    { "EXODUS",     17,  6,
      "Behold, I will stand before thee there upon the rock "
      "in Horeb; and thou shalt smite the rock, and there shall "
      "come water out of it, that the people may drink." },
    // Terry's Battle cites Exodus 17:11 through BibleVerse(); real KJV.
    { "EXODUS",     17, 11,
      "And it came to pass, when Moses held up his hand, "
      "that Israel prevailed: and when he let down his hand, "
      "Amalek prevailed." },
    { "EXODUS",     19, 16, "THUNDER FELL ON THE MOUNT." },
    { "EXODUS",     20,  3, "HAVE NO OTHER GODS." },
    { "EXODUS",     20, 12,
      "Honour thy father and thy mother: that thy days may "
      "be long upon the land which the LORD thy God giveth thee." },

    // Numbers — the wilderness years
    // Terry's Quail cites Numbers 11:11 via BibleVerse(); real KJV.
    { "NUMBERS",    11, 11,
      "And Moses said unto the LORD, Wherefore hast "
      "thou afflicted thy servant? and wherefore have "
      "I not found favour in thy sight, that thou "
      "layest the burden of all this people upon me?" },
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
    { "PSALMS",     23,  1,
      "The LORD is my shepherd; I shall not want. He maketh "
      "me to lie down in green pastures: he leadeth me beside "
      "the still waters. He restoreth my soul: he leadeth me "
      "in the paths of righteousness for his name's sake. "
      "Yea, though I walk through the valley of the shadow of "
      "death, I will fear no evil: for thou art with me; thy "
      "rod and thy staff they comfort me." },
    { "PSALMS",     27,  1,
      "The LORD is my light and my salvation; whom shall I "
      "fear? the LORD is the strength of my life; of whom shall "
      "I be afraid?" },
    { "PSALMS",     46, 10, "STAND STILL AND KNOW." },
    { "PSALMS",    103, 15, "WE FLOWER AND FADE." },
    { "PSALMS",    121,  1,
      "I will lift up mine eyes unto the hills, from whence "
      "cometh my help. My help cometh from the LORD, which "
      "made heaven and earth." },

    // Proverbs — practical wisdom
    { "PROVERBS",    3,  5, "TRUST WITH YOUR WHOLE HEART." },
    { "PROVERBS",   16,  9, "THE STEPS ARE GUIDED." },
    { "PROVERBS",   27, 17, "ONE PERSON SHARPENS ANOTHER." },

    // Isaiah — comfort and vision
    { "ISAIAH",     40,  8, "THE WORD ENDURES." },
    { "ISAIAH",     40, 31, "THOSE WHO WAIT ARE RENEWED." },
    { "ISAIAH",     43,  2, "I AM WITH YOU IN WATERS." },

    // Ecclesiastes — the preacher
    { "ECCLESIASTES",3,  1,
      "To every thing there is a season, and a time to every "
      "purpose under the heaven: A time to be born, and a time "
      "to die; a time to plant, and a time to pluck up that "
      "which is planted." },
    { "ECCLESIASTES",9, 11, "TIME AND CHANCE FOR ALL." },

    // Selected New Testament
    { "MATTHEW",     5,  3,
      "Blessed are the poor in spirit: for theirs is the "
      "kingdom of heaven. Blessed are they that mourn: for "
      "they shall be comforted. Blessed are the meek: for they "
      "shall inherit the earth." },
    { "MATTHEW",     6,  9,
      "Our Father which art in heaven, Hallowed be thy name. "
      "Thy kingdom come. Thy will be done in earth, as it is "
      "in heaven. Give us this day our daily bread. And "
      "forgive us our debts, as we forgive our debtors." },
    { "JOHN",        1,  1,
      "In the beginning was the Word, and the Word was with "
      "God, and the Word was God. The same was in the "
      "beginning with God. All things were made by him; and "
      "without him was not any thing made that was made." },
    { "JOHN",        1,  5, "LIGHT SHINES IN THE DARK." },
    { "JOHN",        3, 16,
      "For God so loved the world, that he gave his only "
      "begotten Son, that whosoever believeth in him should "
      "not perish, but have everlasting life." },
    { "JOHN",       14, 27, "I GIVE YOU PEACE." },
    { "REVELATION", 21,  1,
      "And I saw a new heaven and a new earth: for the first "
      "heaven and the first earth were passed away; and there "
      "was no more sea. And God shall wipe away all tears "
      "from their eyes; and there shall be no more death." },
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
