/*
 * PROC2_$SIGBLOCK - Block additional signals
 *
 * Adds signals to the blocked mask. Returns the old mask value.
 *
 * Parameters:
 *   mask_ptr - Pointer to mask of signals to block
 *   result - Pointer to receive [new_mask, flag]
 *
 * Returns:
 *   Old blocked signal mask value
 *
 * Original address: 0x00e3f63e
 */

#include "proc2.h"

uint32_t PROC2_$SIGBLOCK(uint32_t *mask_ptr, uint32_t *result)
{
    int16_t index;
    proc2_info_t *entry;
    uint32_t old_mask;
    uint32_t new_bits;

    new_bits = *mask_ptr;

    /* Get current process's PROC2 entry */
    index = P2_PID_TO_INDEX(PROC1_$CURRENT);
    entry = P2_INFO_ENTRY(index);

    ML_$LOCK(PROC2_LOCK_ID);

    /* Save old mask and OR in new bits */
    old_mask = entry->sig_blocked_2;
    entry->sig_blocked_2 |= new_bits;

    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Return new mask value */
    result[0] = entry->sig_blocked_2;

    /* Return flag based on flags bit 10 (0x400) */
    result[1] = (entry->flags & 0x0400) ? 1 : 0;

    return old_mask;
}
