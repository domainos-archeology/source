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

#include "pacct_internal.h"

void PACCT_$SHUTDN(void)
{
    status_$t status;
    uint8_t status_buf[8];

    /* Check if accounting is enabled */
    if (pacct_owner.high == UID_$NIL.high &&
        pacct_owner.low == UID_$NIL.low) {
        /* Already disabled, nothing to do */
        return;
    }

    /* Unmap buffer if currently mapped */
    if (DAT_00e81804 != NULL) {
        MST_$UNMAP_PRIVI(1, &UID_$NIL, DAT_00e81804, DAT_00e81800, 0, &status);
    }

    /* Clear buffer state */
    DAT_00e81804 = NULL;    /* map_ptr = NULL */
    DAT_00e81800 = 0;       /* map_offset = 0 */
    DAT_00e817f8 = 0;       /* buf_remaining = 0 */

    /* Unlock the accounting file */
    FILE_$PRIV_UNLOCK(&pacct_owner, (int16_t)DAT_00e817f4, 0x40000,
                      0, 0, 0, status_buf, &status);

    /* Disable accounting by setting owner to nil */
    pacct_owner.high = UID_$NIL.high;
    pacct_owner.low = UID_$NIL.low;
}
