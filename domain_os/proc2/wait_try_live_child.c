/*
 * PROC2_$WAIT_TRY_LIVE_CHILD - Report a stopped or exited child
 *
 * Re-emitted from the image (0x00E3FC5C..0x00E3FD04, 170 bytes).
 *
 * Frame (link.w A6,-0x8):
 *   (0x8,A6)  child_idx word -> D2     (0xA,A6)  options word -> D0
 *   (0xC,A6)  parent_idx word -> D3    (0xE,A6)  prev_idx word (only re-pushed)
 *   (0x10,A6) found -> A2              (0x14,A6) result -> A3
 *   (0x18,A6) pid_ret -> D4
 *
 *   00e3fc7c  *found = FALSE
 *   00e3fc8e  low byte bit 6 (0x0040 stopped) set, bit 5 (0x0020 reported)
 *             clear and options bit 1 set -> set 0x0020, result+0x48 =
 *             (child+0x94 << 8) | 0x7F, *pid_ret = child+0x16, found
 *   00e3fcc2  else: child+0x26 != 0 and != parent+0x1C -> leave (not found)
 *   00e3fcde  flags bit 13 -> REAP_CHILD(child, parent, prev, result, pid), found
 *
 * Sole caller: PROC2_$WAIT 0x00E3FF08.
 *
 * Original address: 0x00e3fc5c
 */

#include "proc2/proc2_internal.h"

void PROC2_$WAIT_TRY_LIVE_CHILD(int16_t child_idx, uint16_t options,
                                 int16_t parent_idx, int16_t prev_idx,
                                 int8_t *found, proc2_wait_result_t *result,
                                 int16_t *pid_ret)
{
    proc2_info_t *child;         /* A0 */

    /* 0x00E3FC7C */
    *found = 0;

    /* 0x00E3FC7E-0x00E3FC8A */
    child = P2_INFO_ENTRY(child_idx);

    /* 0x00E3FC8E-0x00E3FCA2 */
    if ((child->flags & 0x0040) != 0 && (child->flags & 0x0020) == 0 &&
        (options & 0x0002) != 0) {
        child->flags |= 0x0020;                              /* 0x00E3FCA4 */
        /* 0x00E3FCAA-0x00E3FCB6: ext.l, lsl.l #8, ori.b #0x7f */
        result->exit_status = (uint32_t)(((int32_t)(int16_t)child->pad_94 << 8) | 0x7F);
        *pid_ret = (int16_t)child->upid;                     /* 0x00E3FCBC */
        *found = (int8_t)0xFF;                               /* 0x00E3FCFA */
        return;
    }

    /* 0x00E3FCC2-0x00E3FCDC */
    if (child->debugger_idx != 0 &&
        child->debugger_idx != P2_INFO_ENTRY(parent_idx)->self_index) {
        return;
    }

    /* 0x00E3FCDE-0x00E3FCE6 */
    if ((child->flags & PROC2_FLAG_ZOMBIE) == 0) {
        return;
    }

    /* 0x00E3FCE8-0x00E3FCF6 (result slot pushed) */
    PROC2_$WAIT_REAP_CHILD(child_idx, parent_idx, prev_idx, result, pid_ret);
    *found = (int8_t)0xFF;                                   /* 0x00E3FCFA */
}
