/*
 * HINT_$LOOKUP_CACHE - Look up location in local hint cache
 *
 * Checks if a UID's location is in the local cache. The local cache
 * provides faster lookups than the hint file for recently accessed UIDs.
 *
 * Cache entries expire after ~240 clock ticks.
 *
 * Original address: 0x00E49D06
 */

#include "hint/hint_internal.h"

void HINT_$LOOKUP_CACHE(uint32_t *uid_low_masked_ptr, uint8_t *result)
{
    int16_t i;
    int16_t entry_idx;
    hint_cache_entry_t *entry;
    uint32_t uid_key;
    uint32_t time_diff;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    uid_key = *uid_low_masked_ptr;
    entry_idx = 1;
    entry = HINT_$CACHE;

    for (i = HINT_CACHE_SIZE; i >= 0; i--) {
        /* Check if this entry matches our UID */
        if (entry->uid_low_masked == uid_key) {
            /* Found it - copy result */
            *result = entry->result;

            /* Check if entry has expired */
            time_diff = TIME_$CLOCKH - entry->timestamp;
            if (time_diff >= HINT_CACHE_TIMEOUT) {
                /* Entry expired - return not found */
                goto not_found;
            }

            /* Refresh timestamp */
            HINT_$CACHE[entry_idx - 1].timestamp = TIME_$CLOCKH;

            goto done;
        }

        entry_idx++;
        entry++;
    }

not_found:
    *result = 0;

done:
    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}
