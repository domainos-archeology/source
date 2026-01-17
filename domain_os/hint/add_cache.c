/*
 * HINT_$ADD_CACHE - Add entry to local hint cache
 *
 * Adds a lookup result to the local cache for faster future access.
 * Uses round-robin replacement when the cache is full.
 *
 * Original address: 0x00E49D88
 */

#include "hint/hint_internal.h"

void HINT_$ADD_CACHE(uint32_t *uid_low_masked_ptr, uint8_t *result_ptr)
{
    int16_t i;
    int16_t entry_idx;
    hint_cache_entry_t *entry;
    int16_t index_to_use;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    entry_idx = 1;
    entry = HINT_$CACHE;

    /* First, look for an empty slot */
    for (i = HINT_CACHE_SIZE; i >= 0; i--) {
        if (entry->uid_low_masked == 0) {
            /* Found empty slot */
            index_to_use = entry_idx;
            goto fill_entry;
        }

        entry_idx++;
        entry++;
    }

    /* No empty slot - use round-robin replacement */
    HINT_$CACHE_INDEX++;
    if (HINT_$CACHE_INDEX > HINT_CACHE_SIZE) {
        HINT_$CACHE_INDEX = 1;
    }
    index_to_use = HINT_$CACHE_INDEX;

fill_entry:
    /* Fill in the cache entry */
    HINT_$CACHE[index_to_use - 1].uid_low_masked = *uid_low_masked_ptr;
    HINT_$CACHE[index_to_use - 1].result = *result_ptr;
    HINT_$CACHE[index_to_use - 1].timestamp = TIME_$CLOCKH;

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}
