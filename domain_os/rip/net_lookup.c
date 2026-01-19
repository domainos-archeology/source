/*
 * RIP_$NET_LOOKUP - Look up network in routing table
 *
 * This function implements a hash table lookup for routing table entries.
 * The table has 64 entries with linear probing for collision resolution.
 *
 * Original address: 0x00E154E4
 */

#include "rip/rip_internal.h"

/*
 * RIP_$NET_LOOKUP - Look up network in routing table
 *
 * Algorithm:
 * 1. Compute hash index as (network & 0x3F)
 * 2. Linear probe through the table looking for matching network
 * 3. While probing, track first "free" slot (state 0 or 3 for both routes)
 * 4. If found:
 *    - If inc_refcount < 0, increment reference count
 *    - Return pointer to entry
 * 5. If not found and create_if_missing < 0:
 *    - Use tracked free slot (if any)
 *    - Initialize network and mark routes as expired (state 3)
 *    - Set reference count based on inc_refcount flag
 *    - Return pointer to new entry
 * 6. If not found and no creation, return NULL
 *
 * Note: The function does NOT acquire locks - callers must hold the RIP lock.
 */
rip_$entry_t *RIP_$NET_LOOKUP(uint32_t network, int8_t inc_refcount,
                               int16_t create_if_missing)
{
    uint16_t start_idx;     /* Starting hash index */
    uint16_t idx;           /* Current probe index */
    uint16_t free_idx;      /* First free slot found */
    rip_$entry_t *entry;    /* Current entry pointer */
    uint8_t state0, state1; /* Route states */

    /* Compute hash */
    start_idx = (uint16_t)network & RIP_TABLE_MASK;
    free_idx = 0xFFFF;  /* Sentinel: no free slot found yet */
    idx = start_idx;

    /* Linear probe through the table */
    do {
        entry = &RIP_$DATA.entries[idx];

        /* Check for match */
        if (entry->network == network) {
            /* Found matching entry */
            if (inc_refcount < 0) {
                /* Increment reference count */
                RIP_$DATA.ref_counts[idx]++;
            }
            return entry;
        }

        /* Track first free slot for potential insertion */
        if (free_idx == 0xFFFF) {
            /* Check if both route slots are unused (state 0) or expired (state 3) */
            state0 = (entry->routes[0].flags >> RIP_STATE_SHIFT) & 0x03;
            state1 = (entry->routes[1].flags >> RIP_STATE_SHIFT) & 0x03;

            if ((state0 == RIP_STATE_UNUSED || state0 == RIP_STATE_EXPIRED) &&
                (state1 == RIP_STATE_UNUSED || state1 == RIP_STATE_EXPIRED)) {
                free_idx = idx;
            }
        }

        /* Move to next slot (with wrap) */
        idx = (idx + 1) & RIP_TABLE_MASK;
    } while (idx != start_idx);

    /* Not found - create new entry if requested */
    if (create_if_missing < 0 && free_idx != 0xFFFF) {
        entry = &RIP_$DATA.entries[free_idx];

        /* Initialize the entry */
        entry->network = network;

        /* Mark both routes as expired (state 3) */
        entry->routes[0].flags |= RIP_STATE_MASK;  /* 0xC0 = state 3 */
        entry->routes[1].flags |= RIP_STATE_MASK;

        /* Set reference count */
        if (inc_refcount < 0) {
            RIP_$DATA.ref_counts[free_idx] = 0;
        } else {
            RIP_$DATA.ref_counts[free_idx] = 1;
        }

        return entry;
    }

    /* Not found and not created */
    return NULL;
}
