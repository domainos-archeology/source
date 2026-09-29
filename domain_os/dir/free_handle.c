/*
 * DIR_$FREE_HANDLE - Free directory handle slot
 *
 * Returns a handle slot to the free list.  Clears the slot's bit in the
 * in-use bitmap at A5+0x203C, pushes the handle on the free list at
 * A5+0x2038 (unless it is slot 0, the reserve slot DIR_$ALLOC_HANDLE hands
 * out separately), and advances DIR_$WT_FOR_HDNL_EC to wake anybody
 * waiting for a handle.
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
    dir_$handle_t *h = (dir_$handle_t *)handle;     /* A2 */
    uint16_t       slot_idx;                        /* D1 */

    ML_$EXCLUSION_START(&DIR_$MUTEX);               /* 0x00E4B98A */

    /* 0x00E4B998-0x00E4B9A2 */
    slot_idx = h->slot_index;
    DIR_$DATA.handle_in_use &= ~(1u << ((uint32_t)slot_idx & 0x1F));

    /* 0x00E4B9A6-0x00E4B9B0: slot 0 is the reserve slot, which is never
     * chained on the free list. */
    if (slot_idx != 0) {
        h->next = DIR_$DATA.handle_free;
        DIR_$DATA.handle_free = ARCH_PTR_TO_VA(h);
    }

    EC_$ADVANCE(&DIR_$WT_FOR_HDNL_EC);              /* 0x00E4B9B4 */

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);                /* 0x00E4B9C2 */
}
