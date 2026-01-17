/*
 * HINT_$INIT_CACHE - Initialize the local hint cache
 *
 * Initializes the exclusion lock and clears the local cache entries.
 * Called before HINT_$INIT.
 *
 * Original address: 0x00E313C8
 */

#include "hint/hint_internal.h"

void HINT_$INIT_CACHE(void)
{
    int16_t i;
    hint_cache_entry_t *entry;

    /* Initialize the exclusion lock for hint operations */
    ML_$EXCLUSION_INIT(&HINT_$EXCLUSION_LOCK);

    /* Clear all local cache entries */
    entry = HINT_$CACHE;
    for (i = HINT_CACHE_SIZE; i >= 0; i--) {
        entry->uid_low_masked = 0;
        entry->result = 0;
        entry->timestamp = 0;
        entry++;
    }

    /* Initialize cache index to 1 (first usable slot) */
    HINT_$CACHE_INDEX = 1;
}
