/*
 * PACCT_$SHUTDN - Shutdown the process accounting subsystem
 *
 * If accounting is enabled (owner != UID_$NIL):
 *   1. Unmaps the accounting buffer if mapped
 *   2. Clears all buffer state
 *   3. Unlocks the accounting file
 *
 * Either way it then stores UID_$NIL into the owner.  The already-disabled
 * test at 0x00E5A6CC-0x00E5A6DC branches to 0x00E5A732, which is the
 * "movea.l #0xe1737c,A0 / move.l (A0)+,(A5) / move.l (A0)+,(0x4,A5)" tail -
 * NOT the epilogue - so the owner is rewritten with the nil it already holds.
 * A dead store, reproduced as found.  (source-w8xy)
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

    /*
     * 0x00E5A6CC-0x00E5A6DC: two `cmpm.l` against UID_$NIL.  An owner that is
     * already nil skips the whole body and lands on the tail store below.
     */
    if (!(pacct_owner.high == UID_$NIL.high &&
          pacct_owner.low == UID_$NIL.low)) {
        /* 0x00E5A6DE-0x00E5A702: unmap the buffer if one is mapped */
        if (pacct_map_ptr != NULL) {
            MST_$UNMAP_PRIVI(1, &UID_$NIL, ARCH_PTR_TO_VA(pacct_map_ptr),
                             pacct_map_offset, 0, &status);
        }

        /* 0x00E5A706-0x00E5A710 */
        pacct_map_ptr = NULL;       /* clr.l (0x18,A5) */
        pacct_map_offset = 0;       /* clr.l (0x14,A5) */
        pacct_buf_remaining = 0;    /* clr.l (0xc,A5)  */

        /*
         * 0x00E5A712-0x00E5A72C.  `move.l (0x8,A5)` is the lock handle,
         * `move.l #0x40000` covers the mode word 4 and the asid word 0, and
         * the three `clr.l` cover by_key/key, rem_key and rem_node.  The
         * 32 bytes of arguments are never popped - the `unlk` at 0x00E5A742
         * discards them.
         */
        (void)FILE_$PRIV_UNLOCK(&pacct_owner, (int32_t)pacct_lock_handle, 4, 0,
                                0, 0, 0, 0, &dtv_out, &status);
    }

    /* 0x00E5A732-0x00E5A73E: reached from both paths */
    pacct_owner.high = UID_$NIL.high;
    pacct_owner.low = UID_$NIL.low;
}
