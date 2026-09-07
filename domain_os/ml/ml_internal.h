/*
 * ML Internal - Mutual Exclusion Locks Internal Definitions
 *
 * This header contains internal definitions used within the ML subsystem.
 * External code should use ml/ml.h instead.
 */

#ifndef ML_INTERNAL_H
#define ML_INTERNAL_H

#include "ec/ec.h"
#include "misc/misc.h"
#include "ml/ml.h"
#include "proc1/proc1.h"

/*
 * ml_$release_tail - shared lock-release epilogue (0x00E20EB6 .. 0x00E20EEE)
 *
 * In the binary this is not a callable routine; it is a run of straight-line
 * code that BOTH ML_$UNLOCK (which reaches it via `beq.w 0x00E20EB0` /
 * `bra.w 0x00E20EB6` at 0x00E20BAE) and ML_$EXCLUSION_STOP (which reaches it
 * by falling out of 0x00E20EAC) branch into.  It is expressed here as a
 * static inline so both C files emit exactly the same sequence.
 *
 * Entry conditions in the original: A1 = pcb, IPL = 7.
 * Exit: falls out through `andi #-0x701,SR` (SET_IPL0), i.e. it FORCES the
 * interrupt priority level to 0 rather than restoring a saved SR.
 *
 * NOTE: proc1_$reorder_if_needed, proc1_$remove_from_ready_list,
 * proc1_$add_ready_body and PROC1_$DISPATCH_INT2 are all reached with
 * `bsr.w` and take their PCB argument in A1 (register convention);
 * only PROC1_$TRY_TO_SUSPEND is called with a stack argument
 * (`pea (A1)` / `jsr` / `addq.w #4,SP` at 0x00E20ED8).  The C prototypes
 * declare a normal (pcb) parameter; the m68k register-argument forms live
 * live in the proc1 sau2 assembly sources.
 */
static inline void ml_$release_tail(proc1_t *pcb)
{
    uint8_t pri_flags;

    /* 0x00E20EB6: bsr.w 0x00E207D8 */
    proc1_$reorder_if_needed(pcb);

    /* 0x00E20EBA: tst.l (0x40,A1) / bne.b 0x00E20EE6 */
    if (pcb->resource_locks_held == 0) {
        /*
         * 0x00E20EC0: bclr.b #0x4,(0x55,A1)
         * pri_max is the byte at PCB+0x55, so this is bit 4 (0x10) of that
         * byte.  bclr sets Z from the PREVIOUS value of the bit.
         */
        pri_flags = pcb->pri_max;
        pcb->pri_max = (uint8_t)(pri_flags & ~0x10);

        if ((pri_flags & 0x10) != 0) {
            /* 0x00E20EC8 / 0x00E20ECC: re-insert at the un-boosted priority */
            proc1_$remove_from_ready_list(pcb);
            proc1_$add_ready_body(pcb);
        }

        /*
         * 0x00E20ED0: btst.b #0x2,(0x55,A1) -- deferred suspend pending.
         * This is bit 2 (0x04) of the byte at 0x55, NOT 0x400.
         */
        if ((pcb->pri_max & PROC1_FLAG_DEFER_SUSP) != 0) {
            /* 0x00E20ED8: pea (A1) / jsr PROC1_$TRY_TO_SUSPEND / addq.w #4,SP */
            PROC1_$TRY_TO_SUSPEND(pcb);
            /* 0x00E20EE2: movea.l PROC1_$CURRENT_PCB,A1 */
            pcb = PROC1_$CURRENT_PCB;
        }
    }

    /* 0x00E20EE6: bsr.w 0x00E20A24 */
    PROC1_$DISPATCH_INT2(pcb);

    /* 0x00E20EEA: andi #-0x701,SR -- forced IPL 0, not an SR restore */
    SET_IPL0();
}

#endif /* ML_INTERNAL_H */
