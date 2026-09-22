/*
 * PROC2_$SIGNAL_PGROUP - Signal a process group (user entry point)
 *
 * Re-emitted from the image (0x00E3F23E..0x00E3F2C0, 132 bytes).
 *
 * Frame (link.w A6,-0x20; A5 = 0xE7BE84):
 *   (0x8,A6)  pgroup_uid  copied to A6-0x10 and again to A6-0x8
 *   (0xC,A6)  signal ptr  -> A6-0x1E     (0x10,A6) param ptr -> A6-0x18
 *   (0x14,A6) status_ret  <- A6-0x14
 *
 * 0x00E3F282-0x00E3F2A2: pushes &status, `st` (check_perms TRUE), param,
 * signal, then the group index computed by PROC2_$UID_TO_PGROUP_INDEX
 * (&uid copy) and pushed LAST as argument 1.
 *
 * Only reference: the SVC table entry at 0x00E7BA4E.
 *
 * Original address: 0x00e3f23e
 */

#include "proc2/proc2_internal.h"

void PROC2_$SIGNAL_PGROUP(uid_t *pgroup_uid, int16_t *signal, uint32_t *param,
                          status_$t *status_ret)
{
    uid_t uid;                   /* A6-0x8 */
    int16_t sig;                 /* A6-0x1E */
    uint32_t param_copy;         /* A6-0x18 */
    status_$t status;            /* A6-0x14 */

    /* 0x00E3F24A-0x00E3F270 */
    uid.high = pgroup_uid->high;
    uid.low = pgroup_uid->low;
    sig = *signal;
    param_copy = *param;

    /* 0x00E3F274-0x00E3F280 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F282-0x00E3F2A2 */
    PROC2_$SIGNAL_PGROUP_INTERNAL(PROC2_$UID_TO_PGROUP_INDEX(&uid), sig,
                                  param_copy, (int8_t)0xFF, &status);

    /* 0x00E3F2A6-0x00E3F2B6 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
