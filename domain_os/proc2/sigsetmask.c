/*
 * PROC2_$SIGSETMASK - Set signal mask
 *
 * Sets the blocked signal mask. Returns the old mask value.
 * If there are pending signals that become unblocked, they will be delivered.
 *
 * Parameters:
 *   mask_ptr - Pointer to new signal mask
 *   result - Pointer to receive [new_mask, flag]
 *
 * Returns:
 *   Old blocked signal mask value
 *
 * Original address: 0x00e3f6c0
 */

#include "proc2/proc2_internal.h"

uint32_t PROC2_$SIGSETMASK(uint32_t *mask_ptr, uint32_t *result)
{
    int16_t index;
    proc2_info_t *entry;
    uint32_t old_mask;
    uint32_t new_mask;
    uint32_t pending_unblocked;

    new_mask = *mask_ptr;

    /* Get current process's PROC2 entry */
    index = P2_PID_TO_INDEX(PROC1_$CURRENT);
    entry = P2_INFO_ENTRY(index);

    ML_$LOCK(PROC2_LOCK_ID);

    /* Save old mask and set new one */
    old_mask = entry->sig_blocked_2;
    entry->sig_blocked_2 = new_mask;

    /*
     * 0x00E3F70E-0x00E3F718: D1 = ~blocked & pending (the mask is re-read
     * from the entry, not taken from new_mask).
     */
    pending_unblocked = entry->sig_mask_2 & ~entry->sig_blocked_2;
    if (pending_unblocked != 0) {
        /*
         * 0x00E3F71A: subq.l #0x2,SP (Pascal result slot)
         * 0x00E3F71C: move.w (-0xc8,A3),-(SP) -- entry + 0xE4 - 0xC8 =
         *             entry+0x1C, the entry's own 1-based table index.
         * 0x00E3F720: bsr.w 0x00E3ECEA
         */
        PROC2_$DELIVER_PENDING_INTERNAL((int16_t)entry->self_index);
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Return new mask value */
    result[0] = entry->sig_blocked_2;

    /* Return flag based on flags bit 10 (0x400) */
    result[1] = (entry->flags & 0x0400) ? 1 : 0;

    return old_mask;
}
