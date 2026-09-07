/*
 * PROC1_$INHIBIT_END - End an inhibit region
 *
 * Decrements the current PCB's nesting depth.  When it reaches zero the
 * inhibit bit is cleared and the shared release epilogue runs, which may
 * reorder the process in the ready list, undo a priority boost, honour a
 * deferred suspend and dispatch.
 *
 * Must be paired with PROC1_$INHIBIT_BEGIN.
 *
 * Original address: 0x00E20EA2 (14 bytes, plus the shared epilogue at
 * 0x00E20EB0 which it falls into).  The very same four instructions are
 * also the no-waiter block of ML_$EXCLUSION_STOP, which branches here from
 * `blt.b 0x00E20EA2` at 0x00E20E86 -- Ghidra therefore reports this entry
 * as only 14 bytes long.
 *
 * Full instruction trace:
 *   00e20ea2  movea.l (-0x23dc,PC),A1   ; A1 = PROC1_$CURRENT_PCB (0xE1EAC8)
 *   00e20ea6  subq.w #0x1,(0x5a,A1)     ; --nesting_depth
 *   00e20eaa  bne.b 0x00e20eee          ; !=0 -> plain rts, IPL untouched
 *   00e20eac  ori #0x700,SR             ; ==0 -> raise to IPL 7 ...
 *   00e20eb0  bclr.b #0x0,(0x43,A1)     ;         ... clear locks bit 0 ...
 *   00e20eb6  ...                       ;         ... and run the shared tail
 *
 * See proc1_$release_tail() in proc1/proc1.h for 0x00E20EB6..0x00E20EEE.
 */

#include "proc1/proc1_internal.h"

void PROC1_$INHIBIT_END(void)
{
    /* 0x00E20EA2 */
    proc1_t *pcb = PROC1_$CURRENT_PCB;

    /* 0x00E20EA6: subq.w #0x1,(0x5a,A1) -- a 16-bit decrement */
    pcb->nesting_depth--;

    if (pcb->nesting_depth != 0) {
        /* 0x00E20EAA -> 0x00E20EEE: plain rts, the IPL is never changed */
        return;
    }

    /* 0x00E20EAC: only now are interrupts disabled */
    SET_IPL7();

    /*
     * 0x00E20EB0: bclr.b #0x0,(0x43,A1).  0x43 is the least significant byte
     * of the longword at 0x40 (big-endian), so this clears bit 0 of
     * resource_locks_held.
     */
    pcb->resource_locks_held &= ~1u;

    /* 0x00E20EB6: shared epilogue; ends with a forced IPL 0. */
    proc1_$release_tail(pcb);
}
