/*
 * PROC2_$SIGNAL_PGROUP_OS - Signal a process group (kernel entry point)
 *
 * Re-emitted from the image (0x00E3F2C2..0x00E3F336, 118 bytes).  Identical
 * to PROC2_$SIGNAL_PGROUP except that check_perms is pushed as `clr.w`
 * (FALSE) at 0x00E3F2FE.
 *
 * Frame (link.w A6,-0x14; A5 = 0xE7BE84): (0x8,A6) pgroup_uid -> A6-0x8,
 * (0xC,A6) signal -> A6-0x12, (0x10,A6) param -> A6-0x10, (0x14,A6)
 * status_ret <- A6-0xC.
 *
 * Callers: 0x00E671FE, ASKNODE 0x00E64A32, table 0x00E8570C.
 *
 * Original address: 0x00e3f2c2
 */

#include "proc2/proc2_internal.h"

void PROC2_$SIGNAL_PGROUP_OS(uid_t *pgroup_uid, int16_t *signal, uint32_t *param,
                             status_$t *status_ret)
{
    uid_t uid;                   /* A6-0x8 */
    int16_t sig;                 /* A6-0x12 */
    uint32_t param_copy;         /* A6-0x10 */
    status_$t status;            /* A6-0xC */

    /* 0x00E3F2CE-0x00E3F2E6 */
    uid.high = pgroup_uid->high;
    uid.low = pgroup_uid->low;
    sig = *signal;
    param_copy = *param;

    /* 0x00E3F2EA-0x00E3F2F6 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F2F8-0x00E3F318 */
    PROC2_$SIGNAL_PGROUP_INTERNAL(PROC2_$UID_TO_PGROUP_INDEX(&uid), sig,
                                  param_copy, 0, &status);

    /* 0x00E3F31C-0x00E3F32C */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
