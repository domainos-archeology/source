/*
 * PROC2_$GET_SIG_MASK - Get current process signal mask info
 *
 * Fills in the signal mask structure with information about
 * the current process's signal handling state.
 *
 * Parameters:
 *   mask_ret - Pointer to structure to receive signal mask info
 *
 * Original address: 0x00e3f75a
 */

#include "proc2.h"

void PROC2_$GET_SIG_MASK(proc2_sig_mask_t *mask_ret)
{
    int16_t index;
    proc2_info_t *entry;

    /* Get current process's PROC2 entry */
    index = P2_PID_TO_INDEX(PROC1_$CURRENT);
    entry = P2_INFO_ENTRY(index);

    /* Fill in signal mask structure */
    mask_ret->blocked_1 = entry->sig_blocked_1;
    mask_ret->blocked_2 = entry->sig_blocked_2;
    mask_ret->pending = entry->sig_pending;
    mask_ret->mask_1 = entry->sig_mask_1;
    mask_ret->mask_2 = entry->sig_mask_2;
    mask_ret->mask_3 = entry->sig_mask_3;
    mask_ret->mask_4 = entry->sig_mask_4;

    /* Flag from flags bit 10 (0x400) */
    mask_ret->flag_1 = (entry->flags & 0x0400) ? 0xFF : 0x00;

    /* Flag from flags+1 (low byte) bit 2 (0x04) */
    mask_ret->flag_2 = ((entry->flags & 0x0004) != 0) ? 0xFF : 0x00;
}
