/*
 * PROC2_$GET_REGS - Get process registers
 *
 * This function is a stub that always returns permission denied.
 * Full register access is not implemented in this version.
 *
 * Original address: 0x00e41d9e
 */

#include "proc2.h"

void PROC2_$GET_REGS(uid_$t *proc_uid, void *regs, status_$t *status_ret)
{
    (void)proc_uid;
    (void)regs;
    *status_ret = status_$proc2_permission_denied;
}
