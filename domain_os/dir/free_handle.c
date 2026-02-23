/*
 * DIR_$FREE_HANDLE - Free directory handle slot
 *
 * Returns a handle slot to the free list. Clears the active bit
 * in the bitmap, adds to the free chain (unless slot index 0,
 * which is the emergency slot), and advances the wait event
 * counter to wake any processes waiting for a handle.
 *
 * Handle fields used:
 *   +0x30: Next pointer in free list (4 bytes)
 *   +0x38: Slot index (2 bytes)
 *
 * Parameters:
 *   handle - Pointer to handle to free
 *
 * Original address: 0x00E4B980
 * Original size: 86 bytes
 */

#include "dir/dir_internal.h"

void DIR_$FREE_HANDLE(void *handle)
{
    char *base = (char *)__A5_BASE();
    uint8_t *h = (uint8_t *)handle;
    uint16_t slot_idx;

    ML_$EXCLUSION_START(&DIR_$MUTEX);

    /* Get slot index and clear its bit in the active bitmap */
    slot_idx = *(uint16_t *)(h + 0x38);
    {
        uint32_t bit = 1u << (slot_idx & 0x1F);
        *(uint32_t *)(base + 0x203C) &= ~bit;
    }

    /* Add to free list (but not slot 0 - the emergency slot) */
    if (slot_idx != 0) {
        *(uint32_t *)(h + 0x30) = *(uint32_t *)(base + 0x2038);
        *(uint32_t *)(base + 0x2038) = (uint32_t)(uintptr_t)handle;
    }

    /* Wake any waiting processes */
    EC_$ADVANCE(&DIR_$WT_FOR_HDNL_EC);

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);
}
