/*
 * PROC2_$SET_PRIORITY - Set a process's priority range
 *
 * Re-emitted from the image (0x00E414DE..0x00E41570, 148 bytes).
 *
 * Frame (link.w A6,-0xC; A5 = 0xE7BE84):
 *   (0x8,A6)  proc_uid (pushed by value to FIND_INDEX)
 *   (0xC,A6)  priority_1 ptr -> D0     (0x10,A6) priority_2 ptr
 *   (0x14,A6) status_ret  <- A6-0x4
 *   A6-0x8 min, A6-0x6 max: ordered by an UNSIGNED compare (bcc)
 *
 * The status test after FIND_INDEX is `tst.w (-0x2,A6)`, the LOW word;
 * PROC1_$SET_PRIORITY is called with `st` (set = TRUE).
 *
 * Only reference: the SVC table entry at 0x00E7B9DA.
 *
 * Original address: 0x00e414de
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_PRIORITY(uid_t *proc_uid, uint16_t *priority_1, uint16_t *priority_2,
                         status_$t *status_ret)
{
    int16_t index;               /* D2 */
    status_$t status;            /* A6-0x4 */
    uint16_t min_priority;       /* A6-0x8 */
    uint16_t max_priority;       /* A6-0x6 */
    uint16_t d0;

    /* 0x00E414EC-0x00E41508: D0 = *p1; D0 >= *p2 (unsigned) -> swap */
    d0 = *priority_1;
    if (d0 < *priority_2) {
        min_priority = d0;
        max_priority = *priority_2;
    } else {
        min_priority = *priority_2;
        max_priority = d0;
    }

    /* 0x00E4150C-0x00E41518 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E4151A-0x00E41528 */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E4152A: tst.w (-0x2,A6) / bne */
    if ((status & 0xFFFF) == 0) {
        /* 0x00E41530-0x00E41550: (entry+0x9A, TRUE, &min, &max) */
        PROC1_$SET_PRIORITY(P2_INFO_ENTRY(index)->level1_pid, PROC1_SET_PRIORITY_SET,
                            &min_priority, &max_priority);
    }

    /* 0x00E41554-0x00E41564 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
