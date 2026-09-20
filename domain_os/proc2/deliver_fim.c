/*
 * PROC2_$DELIVER_FIM - Deliver Fault Interrupt Message
 *
 * Called by FIM for the current process.  With bit 23 of *status set the
 * caller is asking for the next pending signal: it is fetched with
 * PROC2_$GET_NEXT_PENDING_SIGNAL, *signal_ret and *status are filled in,
 * and bit 23 of *status is set again.  With it clear the caller already has
 * a signal in *signal_ret (a fault): signals 4, 5 and 8 that the process
 * does not handle are either left pending or dropped.  A debugged process
 * then hands the fault to XPD_$CAPTURE_FAULT.  Returns 0xFF when a signal is
 * to be delivered, 0 (after FIM_$ACKNOWLEDGE) when there is none.
 *
 * Parameters (right-to-left pushes, (0x8,A6) = arg 1):
 *   signal_ret    (0x08,A6) A3  word in/out: signal number
 *   status        (0x0C,A6) A2  longword in/out: status / bit 23 request flag
 *   handler_ret   (0x10,A6)     longword out: entry+0x8C
 *   fault_context (0x14,A6)     longword whose ADDRESS goes to XPD
 *   fault_frame   (0x18,A6)     longword whose ADDRESS goes to XPD
 *   mask_ret      (0x1C,A6)     longword out: entry+0x88 or entry+0x78
 *   flag_ret      (0x20,A6)     byte out: flags & 0x0400
 *
 * Original address: 0x00e3edc0 (376 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 * A4 = 0xEA551C + idx*0xE4 = entry + 0xE4, so
 *   (-0xE4,A4) = entry                (-0xBE,A4) = +0x26 debugger_idx
 *   (-0xBA,A4) = +0x2A flags          (-0x74,A4) = +0x70 sig_pending
 *   (-0x70,A4) = +0x74 sig_blocked_1  (-0x6C,A4) = +0x78 sig_blocked_2
 *   (-0x68,A4) = +0x7C sig_mask_3     (-0x64,A4) = +0x80 sig_mask_2
 *   (-0x5C,A4) = +0x88 pad_88         (-0x58,A4) = +0x8C sig_mask_4
 *   (-0x54,A4) = +0x90 sig_status
 */

#include "proc2/proc2_internal.h"

/* 0x00E3EE2A: andi.l #0x3d9dffff */
#define P2_SIG_ALWAYS_POST_MASK  0x3D9DFFFFUL
/* 0x00E3EE6E: andi.w #-0x99,D0w on the low word of the bit -> signals 4, 5, 8 */
#define P2_SIG_FAULT_MASK        0xFFFFFF67UL

/*
 * Bit 23 of the status longword: the byte at offset 1 of the longword
 * (0x00E3EE0C `tst.b (0x1,A2)`, 0x00E3EE54 `bset.b #0x7,(0x1,A2)`).
 */
#define P2_FIM_STATUS_SIGNAL_PENDING  0x00800000L

int8_t PROC2_$DELIVER_FIM(int16_t *signal_ret, status_$t *status,
                          uint32_t *handler_ret, uint32_t fault_context,
                          uint32_t fault_frame, uint32_t *mask_ret,
                          int8_t *flag_ret)
{
    int8_t result;            /* D2b */
    int16_t idx;              /* D3w */
    proc2_info_t *entry;      /* A4 - 0xE4 */
    int16_t sig;              /* D0w */
    uint32_t sig_bit;         /* D1 */
    uint16_t flags;           /* D0w at 0x00E3EED0 */

    /* 0x00E3EDD6 st D2b */
    result = (int8_t)0xFF;

    /* 0x00E3EDD8..0x00E3EDFA: idx = PROC2_$PID_TO_INDEX[PROC1_$CURRENT]; ML_$LOCK(4) */
    idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    ML_$LOCK(PROC2_LOCK_ID);
    entry = P2_INFO_ENTRY(idx);

    for (;;) {
        /* 0x00E3EE0C tst.b (0x1,A2) / bpl */
        if ((*status & P2_FIM_STATUS_SIGNAL_PENDING) == 0) {
            /* 0x00E3EE5C..0x00E3EE94: the caller supplied the signal */
            sig_bit = 0;
            sig_bit |= 1UL << (((uint16_t)*signal_ret - 1) & 31);
            if ((sig_bit & P2_SIG_FAULT_MASK) != 0) {
                goto deliver;
            }
            /* 0x00E3EE76: signal 4, 5 or 8 */
            if ((~entry->sig_blocked_2 & sig_bit) != 0) {
                /* 0x00E3EE80..0x00E3EE8C */
                if ((~entry->sig_blocked_1 & sig_bit) != 0) {
                    goto deliver;
                }
                goto no_signal;
            }
            /* 0x00E3EE90 or.l D1,(-0x64,A4): leave it pending */
            entry->sig_mask_2 |= sig_bit;
            goto no_signal;
        }

        /* 0x00E3EE12..0x00E3EE1E */
        sig = PROC2_$GET_NEXT_PENDING_SIGNAL(entry);
        *signal_ret = sig;
        if (sig == 0) {
            goto no_signal;
        }

        /* 0x00E3EE22..0x00E3EE26 */
        sig_bit = 0;
        sig_bit |= 1UL << (((uint16_t)sig - 1) & 31);

        /* 0x00E3EE28..0x00E3EE44: a signal outside the always-post set that
         * is not in +0x70 is discarded and the scan repeats */
        if ((sig_bit & P2_SIG_ALWAYS_POST_MASK) == 0 &&
            (~entry->sig_pending & sig_bit) != 0) {
            entry->sig_mask_2 &= ~sig_bit;
            continue;
        }

        /* 0x00E3EE46..0x00E3EE5A */
        if (*signal_ret == 0x13) {
            *status = (status_$t)entry->sig_status;
        } else {
            *status = 0;
        }
        *status |= P2_FIM_STATUS_SIGNAL_PENDING;
        break;
    }

deliver:
    /* 0x00E3EE98..0x00E3EECA */
    if (entry->debugger_idx != 0) {
        /* pea (0x18,A6) / pea (0x14,A6): the addresses of the two argument
         * slots are what XPD receives */
        XPD_$CAPTURE_FAULT((void *)&fault_context, (int32_t *)&fault_frame,
                           (uint16_t *)signal_ret, status);
        if (*signal_ret == 0) {
            /* 0x00E3EEB8 */
            result = 0;
            goto unlock;
        }
        sig_bit = 0;
        sig_bit |= 1UL << (((uint16_t)*signal_ret - 1) & 31);
    }

    /* 0x00E3EECC or.l D1,(-0x64,A4) */
    entry->sig_mask_2 |= sig_bit;

    /* 0x00E3EED0..0x00E3EEDE: *flag_ret = (flags & 0x0400) != 0 (sne) */
    flags = entry->flags;
    *flag_ret = ((flags & 0x0400) != 0) ? (int8_t)0xFF : 0;

    /* 0x00E3EEE0..0x00E3EEF4 */
    if ((~entry->sig_mask_3 & sig_bit) == 0 && (flags & 0x0400) == 0) {
        *handler_ret = entry->sig_mask_4;
    }

    /* 0x00E3EEF8..0x00E3EF16: flags 0x4000 selects +0x88 (and is cleared:
     * bclr.b #0x6,(-0xba,A4) on the high byte) else +0x78 */
    if ((flags & 0x4000) != 0) {
        *mask_ret = entry->pad_88;
        entry->flags &= (uint16_t)~0x4000;
    } else {
        *mask_ret = entry->sig_blocked_2;
    }
    goto unlock;

no_signal:
    /* 0x00E3EF18..0x00E3EF1A */
    result = 0;
    FIM_$ACKNOWLEDGE();

unlock:
    /* 0x00E3EF20..0x00E3EF2C */
    ML_$UNLOCK(PROC2_LOCK_ID);
    return result;
}
