/*
 * PROC2_$DELIVER_SIGNAL_INTERNAL - Internal signal delivery
 *
 * Core routine that posts a signal to a process table entry.  In order:
 * wakes a suspended target for 9 / 0x16 / (0x13 with the BLAST status),
 * interrupts a target in "fault mode" for 9 / (0x13 with BLAST), clears the
 * stop signals on 0x16, drops signals the target does not accept, records
 * the 0x13 status, and finally marks the signal pending and hands it to
 * PROC2_$DELIVER_PENDING_INTERNAL unless the target is suspended.
 *
 * A Pascal procedure (callers push 2+2+4+4 bytes, no result slot).
 *
 * Parameters:
 *   proc_index - (0x8,A6)  word: process table index
 *   signal     - (0xA,A6)  word: signal number (1..32)
 *   param      - (0xC,A6)  longword: signal parameter (a status for 0x13)
 *   status_ret - (0x10,A6) status out
 *
 * Original address: 0x00e3eb8c (350 bytes)
 * A3 = 0xEA551C + proc_index*0xE4 = entry + 0xE4, so
 *   (-0xBA/-0xB9,A3) = +0x2A/+0x2B flags high/low byte
 *   (-0xBE,A3) = +0x26 debugger_idx   (-0x4A,A3) = +0x9A level1_pid
 *   (-0x74,A3) = +0x70 sig_pending    (-0x70,A3) = +0x74 sig_blocked_1
 *   (-0x6C,A3) = +0x78 sig_blocked_2  (-0x64,A3) = +0x80 sig_mask_2
 *   (-0x63,A3) = +0x81 byte 1 of sig_mask_2
 *   (-0x54,A3) = +0x90 sig_status     (-0x50,A3) = +0x94 pad_94
 *   (-0x22,A3) = +0xC2 fault_param    (-0x21,A3) = +0xC3 byte 1 of it
 */

#include "proc2/proc2_internal.h"

/* 0x00E3EC4C / 0x00E3EC72: andi.l #-0x1980001 */
#define P2_SIG_STOP_CLEAR_MASK   0xFE67FFFFUL
/* 0x00E3EC9E: andi.l #0x3d9dffff */
#define P2_SIG_ALWAYS_POST_MASK  0x3D9DFFFFUL

void PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t proc_index, int16_t signal,
                                    int32_t param, status_$t *status_ret)
{
    proc2_info_t *entry;      /* A3 - 0xE4 */
    int16_t sig_m1;           /* (-0x12,A6) */
    uint32_t sig_bit;         /* D4 */
    int8_t wake;              /* D0b after the two seq's */

    /* 0x00E3EBA0 */
    *status_ret = status_$ok;

    /* 0x00E3EBA2..0x00E3EBB0: clr.l D4; bset.l D0,D4 (bit number mod 32) */
    sig_m1 = (int16_t)(signal - 1);
    sig_bit = 0;
    sig_bit |= 1UL << ((uint16_t)sig_m1 & 31);

    entry = P2_INFO_ENTRY(proc_index);

    /* 0x00E3EBC2 btst.b #0x6,(-0xb9,A3): flags 0x0040 -- target suspended */
    if ((entry->flags & 0x0040) != 0) {
        /* 0x00E3EBCA..0x00E3EBD8: (signal == 0x16) or (signal == 9), the two
         * seq bytes OR'd and tested with bmi */
        wake = (int8_t)((signal == 0x16 ? 0xFF : 0x00) | (signal == 9 ? 0xFF : 0x00));
        if (wake >= 0) {
            /* 0x00E3EBDA..0x00E3EBE8 */
            wake = (signal == 0x13 && param == status_$fault_process_BLAST) ? -1 : 0;
        }
        if (wake < 0) {
            /* 0x00E3EBEA..0x00E3EBFE */
            entry->flags &= (uint16_t)~0x0040;
            PROC1_$RESUME(entry->level1_pid, status_ret);
        }
    }

    /* 0x00E3EC00 btst.b #0x4,(-0xb9,A3): flags 0x0010 -- target in fault mode */
    if ((entry->flags & 0x0010) != 0) {
        if (signal == 9 ||
            (signal == 0x13 && param == status_$fault_process_BLAST)) {
            /* 0x00E3EC1E move.l param,(-0x22,A3); 0x00E3EC24 bset.b #7,(-0x21,A3) */
            PROC2_FAULT_PARAM_SET(entry, (uint32_t)param | 0x00800000UL);
            /* 0x00E3EC2A */
            entry->pad_94 = (uint16_t)signal;
            /* 0x00E3EC2E */
            entry->flags &= (uint16_t)~0x0010;
            /* 0x00E3EC34..0x00E3EC42: resume and leave */
            PROC1_$RESUME(entry->level1_pid, status_ret);
            return;
        }
    }

    /* 0x00E3EC46: signal 0x16 clears the stop signals from the pending set */
    if (signal == 0x16) {
        entry->sig_mask_2 &= P2_SIG_STOP_CLEAR_MASK;
    }

    /* 0x00E3EC54..0x00E3EC6E: not accepted (bit clear in +0x74) */
    if ((~entry->sig_blocked_1 & sig_bit) == 0) {
        if (signal == 1) {
            /* 0x00E3EC64 bset.b #0x1,(-0xba,A3): high byte bit 1 = 0x0200 */
            entry->flags |= 0x0200;
        }
        if (entry->debugger_idx == 0) {
            return;
        }
    }

    /* 0x00E3EC70..0x00E3EC86: a stop signal clears pending 0x16 and is not
     * posted to a suspended target */
    if ((sig_bit & P2_SIG_STOP_CLEAR_MASK) == 0) {
        /* 0x00E3EC7A bclr.b #0x5,(-0x63,A3): byte 1 of +0x80, bit 5 = bit 21 */
        entry->sig_mask_2 &= ~0x00200000UL;
        if ((entry->flags & 0x0040) != 0) {
            return;
        }
    }

    /* 0x00E3EC88..0x00E3ECA4 */
    if ((~entry->sig_pending & sig_bit) != 0 &&
        (~entry->sig_blocked_2 & sig_bit) != 0 &&
        (sig_bit & P2_SIG_ALWAYS_POST_MASK) == 0) {
        return;
    }

    /* 0x00E3ECA6..0x00E3ECCC: signal 0x13 carries a status */
    if (signal == 0x13) {
        if ((~entry->sig_mask_2 & sig_bit) == 0 &&
            param != status_$fault_process_BLAST) {
            /* 0x00E3ECC0 */
            *status_ret = status_$proc2_another_fault_pending;
            return;
        }
        /* 0x00E3ECC8 */
        entry->sig_status = (uint32_t)param;
    }

    /* 0x00E3ECCE or.l D4,(-0x64,A3) */
    entry->sig_mask_2 |= sig_bit;

    /* 0x00E3ECD2..0x00E3ECDE */
    if ((entry->flags & 0x0040) == 0) {
        PROC2_$DELIVER_PENDING_INTERNAL(proc_index);
    }
}
