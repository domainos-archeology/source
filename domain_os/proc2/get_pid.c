/*
 * PROC2_$GET_PID - Get the PROC1 PID of a process by UID
 *
 * Under the PROC2 lock, looks the UID up and returns entry+0x9A
 * (level1_pid).  When the lookup fails the returned word is whatever D2
 * held on entry (never initialised on that path).
 *
 * Parameters:
 *   proc_uid   (0x8,A6) UID to look up
 *   status_ret (0xC,A6) status from PROC2_$FIND_INDEX
 *
 * Original address: 0x00e40cba (96 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 *   0x00E40CF6  move.w (-0x4a,A0,D1*0x1),D2w  = entry+0x9A level1_pid
 */

#include "proc2/proc2_internal.h"

uint16_t PROC2_$GET_PID(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;            /* D0w/D1w */
    status_$t status;         /* (-0x4,A6) */
    uint16_t pid = 0;         /* D2w: stale register on the error path */

    /* 0x00E40CC8 ML_$LOCK(4) */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E40CD6..0x00E40CE2 */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E40CE4 */
    if (status == status_$ok) {
        pid = P2_INFO_ENTRY(index)->level1_pid;
    }

    /* 0x00E40CFA ML_$UNLOCK(4); 0x00E40D06 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
    return pid;
}
