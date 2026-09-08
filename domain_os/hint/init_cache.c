/*
 * HINT_$INIT_CACHE - Initialize the local hint cache
 *
 * Initializes the exclusion lock, clears the two local cache entries and
 * seeds the round-robin index at globals+0x24 with 1.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E313C8-0x00E313D8  prologue, ML_$EXCLUSION_INIT
 *   0x00E313DA-0x00E313F8  clear loop (moveq #1,D0 / dbf: 2 iterations)
 *   0x00E313FC-0x00E3140A  cache_index = 1, epilogue
 *
 * Original address: 0x00E313C8 (68 bytes)
 */

#include "hint/hint_internal.h"

void HINT_$INIT_CACHE(void)
{
    int16_t i;
    hint_cache_entry_t *entry;   /* A0, biased by +0xC in the image */

    /* 0x00E313CC-0x00E313D8 */
    ML_$EXCLUSION_INIT(&HINT_$EXCLUSION_LOCK);

    /*
     * 0x00E313DA-0x00E313F8: "moveq #0x1,D0" then "dbf D0w" clears exactly
     * HINT_CACHE_SIZE entries.  A third pass would run into
     * hint_globals_t.hintfile_uid at globals+0x18.
     */
    entry = HINT_$CACHE;
    for (i = HINT_CACHE_SIZE - 1; i >= 0; i--) {
        entry->uid_low_masked = 0;   /* 0x00E313E8 clr.l (-0x4,A0) */
        entry->result = 0;           /* 0x00E313EC clr.b (-0x8,A0) */
        entry->timestamp = 0;        /* 0x00E313F0 clr.l (-0xc,A0) */
        entry++;                     /* 0x00E313F4 lea (0xc,A0),A0 */
    }

    /* 0x00E313FC-0x00E31402 */
    HINT_$CACHE_INDEX = 1;
}
