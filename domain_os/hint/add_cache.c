/*
 * HINT_$ADD_CACHE - Add entry to local hint cache
 *
 * Adds a lookup result to the local cache.  The cache holds exactly
 * HINT_CACHE_SIZE (2) 12-byte entries at the base of the HINT globals; the
 * first free entry wins, otherwise the round-robin index at globals+0x24
 * selects the victim.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E49D88-0x00E49DAA  prologue, ML_$EXCLUSION_START
 *   0x00E49DAC-0x00E49DE2  free-entry scan (moveq #1,D0 / dbf: 2 iterations)
 *                          and its store block, then bra to the unlock
 *   0x00E49DE4-0x00E49DF4  round-robin index bump and wrap at 2
 *   0x00E49DF6-0x00E49E2E  the victim entry's three stores
 *   0x00E49E30-0x00E49E44  ML_$EXCLUSION_STOP, epilogue
 *
 * Original address: 0x00E49D88 (190 bytes)
 */

#include "hint/hint_internal.h"

/*
 * The scan is bounded by "moveq #0x1,D0 ... dbf D0w" (0x00E49DAC /
 * 0x00E49DE0), i.e. exactly two entries.  Entry 3 would start at
 * HINT_GLOBALS_BASE + 0x18 = 0xE7DB68, which is hint_globals_t.hintfile_uid,
 * and its uid_low_masked word would land on hintfile_ptr at 0xE7DB70.
 */
_Static_assert(sizeof(hint_cache_entry_t) * HINT_CACHE_SIZE == 0x18,
               "HINT local cache occupies globals+0x00..0x17");
_Static_assert(__builtin_offsetof(hint_globals_t, hintfile_uid) ==
                   sizeof(hint_cache_entry_t) * HINT_CACHE_SIZE,
               "entry HINT_CACHE_SIZE would overlap hintfile_uid");

void HINT_$ADD_CACHE(uint32_t *uid_low_masked_ptr, uint8_t *result_ptr)
{
    int16_t i;
    int16_t entry_idx;          /* D1w, Pascal 1-based */
    hint_cache_entry_t *entry;  /* A0, biased: image holds &entry[i] + 0xC */

    /* 0x00E49D9E-0x00E49DAA */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    /* 0x00E49DAC-0x00E49DB0 */
    entry_idx = 1;
    entry = HINT_$CACHE;

    /*
     * 0x00E49DB4-0x00E49DE0: "moveq #0x1,D0" then "dbf D0w" runs the body
     * twice, once per cache entry.
     */
    for (i = HINT_CACHE_SIZE - 1; i >= 0; i--) {
        if (entry->uid_low_masked == 0) {   /* 0x00E49DB4 tst.l (-0x4,A0) */
            /* 0x00E49DBA-0x00E49DD6 */
            HINT_$CACHE[entry_idx - 1].uid_low_masked = *uid_low_masked_ptr;
            HINT_$CACHE[entry_idx - 1].result = *result_ptr;
            HINT_$CACHE[entry_idx - 1].timestamp = TIME_$CLOCKH;

            /* 0x00E49DD8 bra to the unlock at 0x00E49E30 */
            ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
            return;
        }

        /* 0x00E49DDA-0x00E49DDC */
        entry_idx++;
        entry++;
    }

    /* 0x00E49DE4-0x00E49DF4: bump the round-robin index, wrap above 2 */
    HINT_$CACHE_INDEX++;
    if (HINT_$CACHE_INDEX > HINT_CACHE_SIZE) {
        HINT_$CACHE_INDEX = 1;
    }

    /*
     * 0x00E49DF6-0x00E49E2E: the image recomputes 12*cache_index before each
     * of the three stores rather than branching back to the block above.
     */
    HINT_$CACHE[HINT_$CACHE_INDEX - 1].uid_low_masked = *uid_low_masked_ptr;
    HINT_$CACHE[HINT_$CACHE_INDEX - 1].result = *result_ptr;
    HINT_$CACHE[HINT_$CACHE_INDEX - 1].timestamp = TIME_$CLOCKH;

    /* 0x00E49E30-0x00E49E3A */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}
