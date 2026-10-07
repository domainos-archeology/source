/*
 * ML_$EXCLUSION_STOP - Leave an exclusion region
 *
 * Leaves an exclusion region, waking any waiting processes if there are
 * waiters.  May trigger rescheduling.
 *
 * Original address: 0x00E20E7E (36 bytes, plus the no-waiter block at
 * 0x00E20EA2 and the shared epilogue at 0x00E20EB0).
 *
 * Full instruction trace:
 *   00e20e7e  movea.l (0x4,SP),A0       ; A0 = excl
 *   00e20e82  subq.w #0x1,(0x10,A0)     ; --excl->f5, flags set on the RESULT
 *   00e20e86  blt.b 0x00e20ea2          ; result < 0 -> no waiters
 *   00e20e88  lea (A0),A0
 *   00e20e8a  ori #0x700,SR             ; raise to IPL 7 (no SR saved)
 *   00e20e8e  bsr.w 0x00e2072c          ; ADVANCE_INT(excl)
 *   00e20e92  movea.l (-0x23cc,PC),A1   ; A1 = PROC1_$CURRENT_PCB (0xE1EAC8)
 *   00e20e96  subq.w #0x1,(0x5a,A1)     ; --nesting_depth
 *   00e20e9a  beq.b 0x00e20eb0          ; ==0 -> clear bit + shared tail
 *   00e20e9c  andi #-0x701,SR           ; !=0 -> forced IPL 0 ...
 *   00e20ea0  rts                       ;         ... and return
 *
 *   00e20ea2  movea.l (-0x23dc,PC),A1   ; A1 = PROC1_$CURRENT_PCB
 *   00e20ea6  subq.w #0x1,(0x5a,A1)     ; --nesting_depth
 *   00e20eaa  bne.b 0x00e20eee          ; !=0 -> plain rts (IPL untouched)
 *   00e20eac  ori #0x700,SR             ; ==0 -> raise to IPL 7 ...
 *   00e20eb0  bclr.b #0x0,(0x43,A1)     ;         ... clear locks bit 0 ...
 *   00e20eb6  ...                       ;         ... and run the shared tail
 *
 * Note that on the no-waiter path interrupts are NOT disabled until
 * 0x00E20EAC, and the early return at 0x00E20EEE leaves the IPL alone.
 * See proc1_$release_tail() in proc1/proc1.h for the shared exit path.
 */

#include "ml/ml_internal.h"

void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)
{
    proc1_t *pcb;
    int16_t new_state;

    /* 0x00E20E82/0x00E20E86: branch is on the decremented value */
    excl->f5--;
    new_state = excl->f5;

    if (new_state >= 0) {
        /*
         * There were waiters: wake them up.
         */
        /* 0x00E20E8A */
        SET_IPL7();

        /* 0x00E20E8E: the exclusion record's first 12 bytes are an EC */
        /* 0x00E20E8E `bsr ADVANCE_INT` with A0 = excl: a register call, made
         * from C through the inline wrapper in ec/ec.h (source-rg5a) */
        ADVANCE_INT((ec_$eventcount_t *)excl);

        /* 0x00E20E92 */
        pcb = PROC1_$CURRENT_PCB;

        /* 0x00E20E96 */
        pcb->nesting_depth--;

        if (pcb->nesting_depth != 0) {
            /* 0x00E20E9C: forced IPL 0, then rts */
            SET_IPL0();
            return;
        }
    } else {
        /* 0x00E20EA2 */
        pcb = PROC1_$CURRENT_PCB;

        /* 0x00E20EA6 */
        pcb->nesting_depth--;

        if (pcb->nesting_depth != 0) {
            /* 0x00E20EAA -> 0x00E20EEE: plain rts, IPL never changed */
            return;
        }

        /* 0x00E20EAC: only now are interrupts disabled on this path */
        SET_IPL7();
    }

    /*
     * 0x00E20EB0: bclr.b #0x0,(0x43,A1) -- 0x43 is the least significant
     * byte of the longword at 0x40, so this clears bit 0 of
     * resource_locks_held.  Unlike ML_$UNLOCK, both paths here reach the
     * tail only when nesting_depth hit zero, so the clear is unconditional.
     */
    pcb->resource_locks_held &= ~1u;

    /* 0x00E20EB6: shared epilogue; ends with a forced IPL 0. */
    proc1_$release_tail(pcb);
}
