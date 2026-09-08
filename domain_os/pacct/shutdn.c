/*
 * PACCT_$SHUTDN - Shutdown the process accounting subsystem
 *
 * If accounting is enabled (owner != UID_$NIL):
 *   1. Unmaps the accounting buffer if mapped
 *   2. Clears all buffer state
 *   3. Unlocks the accounting file
 *   4. Sets owner to UID_$NIL to disable accounting
 *
 * Original address: 0x00E5A6C0
 * Size: 134 bytes
 */

#include "pacct/pacct_internal.h"

void PACCT_$SHUTDN(void)
{
    status_$t status;
    /* (-0x8,A6): FILE_$PRIV_UNLOCK's data-time-valid longword out. */
    uint32_t dtv_out;

    /* Check if accounting is enabled */
    if (pacct_owner.high == UID_$NIL.high &&
        pacct_owner.low == UID_$NIL.low) {
        /* Already disabled, nothing to do */
        return;
    }

    /* Unmap buffer if currently mapped */
    if (pacct_map_ptr != NULL) {
        MST_$UNMAP_PRIVI(1, &UID_$NIL, ARCH_PTR_TO_VA(pacct_map_ptr), pacct_map_offset, 0, &status);
    }

    /* Clear buffer state */
    pacct_map_ptr = NULL;    /* map_ptr = NULL */
    pacct_map_offset = 0;       /* map_offset = 0 */
    pacct_buf_remaining = 0;       /* buf_remaining = 0 */

    /* Unlock the accounting file */
    /* `move.l (0x8,A5)` slot, `move.l #0x40000` = mode word 4 + asid word 0,
     * then three `clr.l`. */
    (void)FILE_$PRIV_UNLOCK(&pacct_owner, (int32_t)pacct_lock_handle, 4, 0,
                            0, 0, 0, 0, &dtv_out, &status);

    /* Disable accounting by setting owner to nil */
    pacct_owner.high = UID_$NIL.high;
    pacct_owner.low = UID_$NIL.low;
}
