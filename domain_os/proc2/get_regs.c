/*
 * PROC2_$GET_REGS - Get process registers
 *
 * A stub in this build: it reads none of its arguments and returns
 * status_$proc2_permission_denied.  The frame (link -0x9C) is allocated
 * but never touched.
 *
 * Parameters (six 4-byte stack slots; only the sixth is used):
 *   proc_uid   (0x08,A6)
 *   arg2..arg5 (0x0C..0x18,A6) unused
 *   status_ret (0x1C,A6) status out
 *
 * Original address: 0x00e41d9e (30 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_REGS(uid_t *proc_uid, void *arg2, void *arg3, void *arg4,
                     void *arg5, status_$t *status_ret)
{
    (void)proc_uid;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;

    /* 0x00E41DAA..0x00E41DAE */
    *status_ret = status_$proc2_permission_denied;
}
