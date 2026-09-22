/*
 * PROC2_$WAIT_TRY_ZOMBIE - Report a child on the debug-target list
 *
 * Re-emitted from the image (0x00E3FD06..0x00E3FDCE, 202 bytes).
 *
 * Frame (link.w A6,-0x8):
 *   (0x8,A6)  idx word -> D2       (0xA,A6)  options word: never read
 *   (0xC,A6)  found -> A4          (0x10,A6) result -> A3
 *   (0x14,A6) pid_ret -> D3
 *
 *   00e3fd1e  *found = FALSE
 *   00e3fd30  flags bit 13 (zombie):
 *     00e3fd3a   bit 15 set   -> REAP_CHILD(idx, 0, 0, result, pid) (`clr.l`
 *                               covers both word arguments), found
 *     00e3fd4e   bit 15 clear -> DEBUG_CLEAR_INTERNAL(+0x1C, FALSE);
 *                               result+0x48/+0x4C = child+0x98/+0x9C;
 *                               result+0x38 = UID_$NIL; *pid = +0x16; found
 *   00e3fd7a  not a zombie: low byte bit 4 (0x0010) set and bit 5 (0x0020)
 *             clear -> set 0x0020; result+0x48 = (child+0x94 << 8) | 0x7F;
 *             result+0x4C = fault param (child+0xC2) with bit 23 cleared
 *             (bclr.b #7,(0x4d,A3)); result+0x64 = TRUE if bit 23 was set
 *             (tst.b (-0x21,A2)); result+0x40 = child+0x00; *pid = +0x16; found
 *   otherwise nothing.
 *
 * Sole caller: PROC2_$WAIT 0x00E3FF9E.
 *
 * Original address: 0x00e3fd06
 */

#include "proc2/proc2_internal.h"

void PROC2_$WAIT_TRY_ZOMBIE(int16_t zombie_idx, uint16_t options,
                             int8_t *found, proc2_wait_result_t *result,
                             int16_t *pid_ret)
{
    proc2_info_t *child;         /* A2 */
    uint16_t flags;              /* D0 */
    uint32_t fault_param;

    (void)options;               /* (0xA,A6): never read */

    /* 0x00E3FD1E */
    *found = 0;

    /* 0x00E3FD20-0x00E3FD30 */
    child = P2_INFO_ENTRY(zombie_idx);
    flags = child->flags;

    /* 0x00E3FD34: btst #13 */
    if ((flags & PROC2_FLAG_ZOMBIE) != 0) {
        /* 0x00E3FD3A: tst.w D0w / bpl */
        if ((int16_t)flags < 0) {
            /* 0x00E3FD3E-0x00E3FD48 */
            PROC2_$WAIT_REAP_CHILD(zombie_idx, 0, 0, result, pid_ret);
        } else {
            /* 0x00E3FD4E-0x00E3FD54 (no result slot) */
            DEBUG_CLEAR_INTERNAL((int16_t)child->self_index, 0);
            /* 0x00E3FD58-0x00E3FD60 */
            result->exit_status = PROC2_ZOMBIE_EXIT_98(child);
            result->exit_info = PROC2_ZOMBIE_EXIT_9C(child);
            /* 0x00E3FD64-0x00E3FD6E */
            result->parent_uid.high = UID_$NIL.high;
            result->parent_uid.low = UID_$NIL.low;
            /* 0x00E3FD72-0x00E3FD74 */
            *pid_ret = (int16_t)child->upid;
        }
        *found = (int8_t)0xFF;                               /* 0x00E3FDC4 */
        return;
    }

    /* 0x00E3FD7A-0x00E3FD84 */
    if ((flags & 0x0010) == 0 || (flags & 0x0020) != 0) {
        return;                                              /* 0x00E3FDC6 */
    }

    /* 0x00E3FD86 */
    child->flags |= 0x0020;

    /* 0x00E3FD8C-0x00E3FD98 */
    result->exit_status = (uint32_t)(((int32_t)(int16_t)child->pad_94 << 8) | 0x7F);

    /* 0x00E3FD9C-0x00E3FDA2: copy the longword at +0xC2, clear its bit 23 */
    fault_param = PROC2_FAULT_PARAM_GET(child);
    result->exit_info = fault_param & ~0x00800000u;

    /* 0x00E3FDA8-0x00E3FDAE: bit 7 of byte +0xC3 = bit 23 of the longword */
    if ((fault_param & 0x00800000u) != 0) {
        result->flag_64 = (int8_t)0xFF;
    }

    /* 0x00E3FDB2-0x00E3FDBA: child+0x00 */
    result->child_uid.high = child->uid.high;
    result->child_uid.low = child->uid.low;

    /* 0x00E3FDBE-0x00E3FDC0 */
    *pid_ret = (int16_t)child->upid;
    *found = (int8_t)0xFF;                                   /* 0x00E3FDC4 */
}
