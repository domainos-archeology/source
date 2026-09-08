/*
 * HINT_$LOOKUP_CACHE - Look up location in local hint cache
 *
 * Scans the two local cache entries for one holding the given key.  A match
 * copies the cached result byte out, but is only accepted when the entry is
 * younger than HINT_CACHE_TIMEOUT ticks; an expired match falls back into the
 * scan, and a scan that ends without an accepted match clears the result.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E49D06-0x00E49D28  prologue, ML_$EXCLUSION_START
 *   0x00E49D2A-0x00E49D34  loop setup (moveq #1,D0 -> 2 iterations)
 *   0x00E49D38-0x00E49D64  match test, age test, timestamp refresh
 *   0x00E49D66-0x00E49D70  loop step / dbf, then "clr.b (A2)"
 *   0x00E49D72-0x00E49D86  ML_$EXCLUSION_STOP, epilogue
 *
 * Original address: 0x00E49D06 (130 bytes)
 */

#include "hint/hint_internal.h"

void HINT_$LOOKUP_CACHE(uint32_t *uid_low_masked_ptr, uint8_t *result)
{
    int16_t i;
    int16_t entry_idx;          /* D1w, Pascal 1-based */
    hint_cache_entry_t *entry;  /* A0, biased by +0xC in the image */
    int32_t age;                /* D3 */

    /* 0x00E49D1C-0x00E49D28 */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    /* 0x00E49D2A-0x00E49D34 */
    entry_idx = 1;
    entry = HINT_$CACHE;

    /*
     * 0x00E49D38-0x00E49D6C: "moveq #0x1,D0" then "dbf D0w" runs the body
     * twice, once per cache entry.
     */
    for (i = HINT_CACHE_SIZE - 1; i >= 0; i--) {
        /*
         * 0x00E49D38-0x00E49D40: the image re-loads the key pointer from D2
         * into A3 on every pass and compares through it.
         */
        if (entry->uid_low_masked == *uid_low_masked_ptr) {
            /* 0x00E49D42 */
            *result = entry->result;

            /*
             * 0x00E49D46-0x00E49D52: signed longword age test.  An expired
             * entry branches to the loop step at 0x00E49D66, so the scan
             * continues rather than giving up.
             */
            age = (int32_t)(TIME_$CLOCKH - entry->timestamp);
            if (age < HINT_CACHE_TIMEOUT) {
                /* 0x00E49D54-0x00E49D64: refresh, then bra to the unlock */
                HINT_$CACHE[entry_idx - 1].timestamp = TIME_$CLOCKH;
                ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
                return;
            }
        }

        /* 0x00E49D66-0x00E49D68 */
        entry_idx++;
        entry++;
    }

    /* 0x00E49D70: no fresh match - the scan always clears the result byte */
    *result = 0;

    /* 0x00E49D72-0x00E49D7C */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}
