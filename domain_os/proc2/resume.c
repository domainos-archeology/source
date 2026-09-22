/*
 * PROC2_$RESUME - Resume a suspended process
 *
 * Re-emitted from the image (0x00E413DA..0x00E41466, 142 bytes).
 *
 * Frame (link.w A6,-0x8; A5 = 0xE7BE84): (0x8,A6) proc_uid (pushed by
 * value to FIND_INDEX), (0xC,A6) status_ret; A6-0x4 status.  Both error
 * tests are `tst.w (-0x2,A6)`, the LOW word of the status.
 *
 * Only reference: the SVC table entry at 0x00E7B4DA.
 *
 * Original address: 0x00e413da
 */

#include "proc2/proc2_internal.h"

void PROC2_$RESUME(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;               /* D2 */
    status_$t status;            /* A6-0x4 */

    /* 0x00E413E8-0x00E413F4 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E413F6-0x00E41404 */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E41406: tst.w (-0x2,A6) / bne -> unlock */
    if ((status & 0xFFFF) == 0) {
        /* 0x00E4140C-0x00E41428: PROC1_$RESUME(entry+0x9A, &status), result slot */
        PROC1_$RESUME(P2_INFO_ENTRY(index)->level1_pid, &status);

        /* 0x00E4142A: tst.w (-0x2,A6) / beq */
        if ((status & 0xFFFF) != 0) {
            /* 0x00E41430: cmpi.l #0xa0003 */
            if (status == status_$process_not_suspended) {
                status = status_$proc2_not_suspended;        /* 0x00E4143A */
            } else {
                status |= (status_$t)0x80000000u;            /* 0x00E41444: bset.b #7 */
            }
        }
    }

    /* 0x00E4144A-0x00E4145A */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
