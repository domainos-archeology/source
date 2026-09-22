/*
 * PROC2_$WAIT_REAP_CHILD - Free a finished child and report its exit data
 *
 * Re-emitted from the image (0x00E3FB34..0x00E3FC5A, 296 bytes).
 *
 * Frame (link.w A6,-0x8; A5 inherited = 0xE7BE84):
 *   (0x8,A6)  child_idx    word -> D2    (0xA,A6) parent_idx word -> D3
 *   (0xC,A6)  prev_sibling word -> D4    (0xE,A6) result -> A2
 *   (0x12,A6) pid_ret
 *   A3 = child entry biased (muls); A0 = 0xEA551C is kept for the mulu/muls
 *   lookups of the neighbours.
 *
 *   00e3fb5c  child+0x26 != 0 -> DEBUG_CLEAR_INTERNAL(child+0x1C, FALSE)
 *   00e3fb6e  unlink from the allocated list; the back link at 0x00E3FBA6
 *             is written unconditionally (next 0 -> entry(0)+0x14)
 *   00e3fbac  tst.w flags / bmi: only when bit 15 (0x8000) is CLEAR is the
 *             child unlinked from parent's child list (prev 0 -> parent+0x20,
 *             else prev+0x22)
 *   00e3fbd8  PGROUP_CLEANUP_INTERNAL(child, 1)
 *   00e3fbe8  bclr.b #5,(-0xba,A3): HIGH byte bit 5 -> flags &= ~0x2000
 *   00e3fbee  push onto the free list
 *   00e3fbf8  result+0x48 = 2 longwords from child+0x98
 *   00e3fc04  result+0x50 = 5 longwords from child+0xA4
 *   00e3fc16  result+0x38 = child+0x08
 *   00e3fc22  result+0x00 = 14 longwords from child+0x60
 *   00e3fc32  result+0x65 = smi(word at child+0xA0); +0x66 = sne(bit 14 of it)
 *   00e3fc4a  *pid_ret = child+0x16
 *
 * Callers: WAIT_TRY_LIVE_CHILD 0x00E3FCF6, WAIT_TRY_ZOMBIE 0x00E3FD48.
 *
 * Original address: 0x00e3fb34
 */

#include "proc2/proc2_internal.h"

void PROC2_$WAIT_REAP_CHILD(int16_t child_idx, int16_t parent_idx,
                            int16_t prev_sibling, proc2_wait_result_t *result,
                            int16_t *pid_ret)
{
    proc2_info_t *child;         /* A3 */
    proc2_info_t *other;         /* A1 */
    uint16_t word_a0;
    int i;

    /* 0x00E3FB4C-0x00E3FB58 */
    child = P2_INFO_ENTRY(child_idx);

    /* 0x00E3FB5C-0x00E3FB6C (no result slot) */
    if (child->debugger_idx != 0) {
        DEBUG_CLEAR_INTERNAL((int16_t)child->self_index, 0);
    }

    /* 0x00E3FB6E-0x00E3FB8E */
    if (child->pad_14 == 0) {
        P2_INFO_ALLOC_PTR = child->next_index;
    } else {
        other = P2_INFO_ENTRY((int16_t)child->pad_14);       /* mulu */
        other->next_index = child->next_index;
    }
    /* 0x00E3FB94-0x00E3FBA6: unconditional */
    other = P2_INFO_ENTRY((int16_t)child->next_index);       /* mulu */
    other->pad_14 = child->pad_14;

    /* 0x00E3FBAC: tst.w (-0xba,A3) / bmi 0x00E3FBD8 */
    if ((int16_t)child->flags >= 0) {
        if (prev_sibling == 0) {                             /* 0x00E3FBB2 */
            other = P2_INFO_ENTRY(parent_idx);               /* muls */
            other->first_child_idx = child->next_child_sibling;   /* 0x00E3FBC0 */
        } else {
            other = P2_INFO_ENTRY(prev_sibling);             /* muls */
            other->next_child_sibling = child->next_child_sibling;   /* 0x00E3FBD2 */
        }
    }

    /* 0x00E3FBD8-0x00E3FBE6 */
    PGROUP_CLEANUP_INTERNAL(child, 1);

    /* 0x00E3FBE8 */
    child->flags &= (uint16_t)~PROC2_FLAG_ZOMBIE;

    /* 0x00E3FBEE-0x00E3FBF4 */
    child->next_index = P2_FREE_LIST_HEAD;
    P2_FREE_LIST_HEAD = (uint16_t)child_idx;

    /* 0x00E3FBF8-0x00E3FC00: child+0x98 / +0x9C */
    result->exit_status = PROC2_ZOMBIE_EXIT_98(child);
    result->exit_info = PROC2_ZOMBIE_EXIT_9C(child);

    /* 0x00E3FC04-0x00E3FC12: moveq #4 / dbf = 20 bytes from child+0xA4 */
    for (i = 0; i < 20; i++) {
        result->usage[i] = child->zombie_usage[i];
    }

    /* 0x00E3FC16-0x00E3FC1E: child+0x08 */
    result->parent_uid.high = child->parent_uid.high;
    result->parent_uid.low = child->parent_uid.low;

    /* 0x00E3FC22-0x00E3FC2E: moveq #0xd / dbf = 56 bytes from child+0x60 */
    for (i = 0; i < 0x38; i++) {
        result->entry_60[i] = ((const uint8_t *)&child->tty_uid)[i];
    }

    /* 0x00E3FC32-0x00E3FC46: the word at child+0xA0 (zombie_pad[2..3]) */
    word_a0 = (uint16_t)(((uint16_t)child->zombie_pad[2] << 8) | child->zombie_pad[3]);
    result->flag_65 = ((word_a0 & 0x8000) != 0) ? (int8_t)0xFF : 0;   /* smi */
    result->flag_66 = ((word_a0 & 0x4000) != 0) ? (int8_t)0xFF : 0;   /* sne */

    /* 0x00E3FC4A-0x00E3FC4E */
    *pid_ret = (int16_t)child->upid;
}
