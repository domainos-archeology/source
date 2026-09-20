/*
 * PROC2_$GET_SIG_MASK - Copy the current process's signal state
 *
 * Fills a proc2_sig_mask_t from the current process's table entry.
 *
 * Parameters:
 *   mask_ret - (0x8,A6) A0: 0x1E-byte output record
 *
 * Original address: 0x00e3f75a (132 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 * A1 = 0xEA551C + idx*0xE4 = entry + 0xE4 (idx = PROC2_$PID_TO_INDEX[PROC1_$CURRENT]):
 *   0x00E3F796  (A2)+ = (-0x70,A1) = +0x74 sig_blocked_1  -> mask+0x00
 *   0x00E3F798  (A2)+ = +0x78 sig_blocked_2               -> mask+0x04
 *   0x00E3F79C  (-0x74,A1) = +0x70 sig_pending            -> mask+0x08
 *   0x00E3F7A2  (-0x60,A1) = +0x84 sig_mask_1             -> mask+0x0C
 *   0x00E3F7A8  (-0x64,A1) = +0x80 sig_mask_2             -> mask+0x10
 *   0x00E3F7AE  (-0x68,A1) = +0x7C sig_mask_3             -> mask+0x14
 *   0x00E3F7B4  (-0x58,A1) = +0x8C sig_mask_4             -> mask+0x18
 *   0x00E3F7BA  btst.l #0xa of the flags word / sne       -> mask+0x1C
 *   0x00E3F7C8  btst.b #0x2,(-0xb9,A1) (flags 0x0004) / sne -> mask+0x1D
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_SIG_MASK(proc2_sig_mask_t *mask_ret)
{
    int16_t idx;
    proc2_info_t *entry;

    idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    entry = P2_INFO_ENTRY(idx);

    mask_ret->blocked_1 = entry->sig_blocked_1;
    mask_ret->blocked_2 = entry->sig_blocked_2;
    mask_ret->pending = entry->sig_pending;
    mask_ret->mask_1 = entry->sig_mask_1;
    mask_ret->mask_2 = entry->sig_mask_2;
    mask_ret->mask_3 = entry->sig_mask_3;
    mask_ret->mask_4 = entry->sig_mask_4;
    mask_ret->flag_1 = ((entry->flags & 0x0400) != 0) ? (int8_t)0xFF : 0;
    mask_ret->flag_2 = ((entry->flags & 0x0004) != 0) ? (int8_t)0xFF : 0;
}
