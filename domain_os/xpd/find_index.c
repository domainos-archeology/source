/*
 * xpd/find_index.c - XPD_$FIND_INDEX (0x00E5AF38, 102 bytes)
 *
 * The PROC2 index of a target the caller may operate on: the caller must be
 * its debugger and it must be stopped.  The index is returned whatever the
 * status (0x00E5AF54 `move.w D0w,D1w` only after a good PROC2 lookup, but
 * D0 is left as PROC2_$FIND_INDEX returned it on every path).
 */

#include "xpd/xpd_internal.h"

int16_t XPD_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;                      /* D0 */
    proc2_info_t *entry;                /* A0 (+0xE4) */

    /* 0x00E5AF42-0x00E5AF52 */
    index = PROC2_$FIND_INDEX(proc_uid, status_ret);
    if (*status_ret != status_$ok) {
        return index;
    }

    /* 0x00E5AF54-0x00E5AF86: the target's debugger must be this process. */
    entry = XPD_ENTRY(index);
    if (entry->debugger_idx != XPD_CURRENT_INDEX()) {
        *status_ret = status_$proc2_proc_not_debug_target;
        return index;
    }

    /* 0x00E5AF88-0x00E5AF90: `btst.b #0x4,(-0xb9,A0)` - flags bit 4 */
    if ((entry->flags & XPD_PF_SUSPENDED) == 0) {
        *status_ret = status_$xpd_target_not_suspended;
    }

    return index;
}
