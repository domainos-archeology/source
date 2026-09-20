/*
 * PROC2_$AWAKEN_GUARDIAN - Awaken the guardian/debugger process
 *
 * Notifies the guardian (debugger, or failing that the parent) of the
 * process at *proc_index: sends it signals 0x12 and 0x17 and advances its
 * creation-record eventcount; when the guardian is not a debugger, also
 * advances this process's own fork eventcount (twice if it is still zero).
 *
 * Parameters:
 *   proc_index - Pointer to the target's process table index
 *
 * Original address: 0x00e3e960 (204 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 * A2 = 0xEA551C + *proc_index * 0xE4 = entry + 0xE4, so
 *   (-0xBE,A2) = entry+0x26 debugger_idx      (-0xC6,A2) = entry+0x1E parent idx
 *   (-0xB9,A2) = entry+0x2B flags low byte    (-0xC8,A2) = entry+0x1C self_index
 */

#include "proc2/proc2_internal.h"

void PROC2_$AWAKEN_GUARDIAN(int16_t *proc_index)
{
    proc2_info_t *entry;
    int16_t guardian_idx;      /* D2 */
    int32_t param;             /* (-0x8,A6): cleared once, passed by value */
    status_$t status;          /* (-0x4,A6): written by the callee, never read */
    ec_$eventcount_t *ec;

    /* 0x00E3E96E..0x00E3E990: guardian = debugger, else parent */
    entry = P2_INFO_ENTRY(*proc_index);
    guardian_idx = (int16_t)entry->debugger_idx;
    if (guardian_idx == 0) {
        guardian_idx = (int16_t)entry->parent_pgroup_idx;
    }

    /* 0x00E3E990 bclr.b #0x5,(-0xb9,A2): flags bit 5 (0x0020) */
    entry->flags &= (uint16_t)~0x0020;

    if (guardian_idx != 0) {
        /* 0x00E3E99A..0x00E3E9C6: two signals, param = 0 by value */
        param = 0;
        PROC2_$DELIVER_SIGNAL_INTERNAL(guardian_idx, 0x12, param, &status);
        PROC2_$DELIVER_SIGNAL_INTERNAL(guardian_idx, 0x17, param, &status);

        /* 0x00E3E9CA..0x00E3E9E6: pea (-0xc,A0,D0) with A0 = 0xE2B978,
         * D0 = guardian*0x18 -> PROC2_$EC[guardian-1].cr_rec_ec */
        EC_$ADVANCE(PROC_CR_REC_EC(guardian_idx));
    }

    /* 0x00E3E9E8: cmp.w (-0xbe,A2),D2w -- only when the guardian is not the
     * debugger (i.e. it is the parent, or both are zero and this is skipped) */
    if (guardian_idx != (int16_t)entry->debugger_idx) {
        /* 0x00E3E9EE..0x00E3EA0A: pea (-0x18,A2) with A2 = 0xE2B978 +
         * self_index*0x18 -> PROC2_$EC[self-1].fork_ec */
        ec = PROC_FORK_EC(entry->self_index);
        EC_$ADVANCE(ec);
        /* 0x00E3EA12 tst.l (-0x18,A2): advance again while the value is 0 */
        if (ec->value == 0) {
            EC_$ADVANCE(ec);
        }
    }
}
