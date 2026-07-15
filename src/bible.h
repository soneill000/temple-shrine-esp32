// bible.h — small curated verse table used by After Egypt scenes and
// Oracle. Provides a lookup by citation and a random-verse pick.
//
// The `text` field is a paraphrased thematic rendering in my own
// words, NOT the KJV text itself. To swap in the actual KJV (which is
// genuinely public domain, 1611): pull the referenced verses from a
// PD source such as Project Gutenberg's Bible-kjv.txt and replace the
// text field of each entry. Citations are already the real book /
// chapter / verse and match Terry's BibleVerse() usage where known.

#pragma once

typedef struct {
    const char *book;
    int         chapter;
    int         verse;
    const char *text;
} kjv_verse_t;

extern const kjv_verse_t KJV_TABLE[];
extern const int         KJV_TABLE_N;

// Return the first entry whose citation matches, or NULL if not found.
const kjv_verse_t *kjv_lookup(const char *book, int chapter, int verse);

// Return a random entry.
const kjv_verse_t *kjv_random(void);
