/*
 * proc1_$reorder_if_needed - Move a PCB if its neighbours now outrank it
 * Original address: 0x00e207d8 (72 bytes)
 *
 * The body of PROC1_$REORDER_READY (0x00E207D4).  Register convention:
 * A1 = pcb; `bsr'd from proc1_$set_lock_body (0x00E20B0A) and the
 * ML_$UNLOCK / ML_$EXCLUSION_STOP tail (0x00E20EB6).
 *
 * 0x00E207D8  D1 = (0x40,A1)                          pcb->resource_locks_held
 * 0x00E207DC  cmpa.l (-0x1ba4,PC),A1 / beq 0x00E207FE  pcb is the list head:
 *                                                      skip the prev test
 * 0x00E207E2  A0 = (0x4,A1)                           prev
 * 0x00E207E6  cmp.l (0x40,A0),D1 / bhi 0x00E207F8      more locks than prev
 * 0x00E207EC  bne.b 0x00E207FE                        fewer: prev is fine
 * 0x00E207EE  cmp.w (0x52,A0),D0 / bls 0x00E207FE      state <= prev's: fine
 * 0x00E207F8  bsr.w remove; bra.b insert_into_ready_list (0x00E20844)
 * 0x00E207FE  A0 = (A1)                               next
 * 0x00E20800  cmp.l (0x40,A0),D1 / bhi 0x00E2087A      more locks than next: rts
 * 0x00E20806  bne.b 0x00E20812                        fewer: move back
 * 0x00E20808  cmp.w (0x52,A0),D0 / bcc 0x00E2087A      state >= next's: rts
 * 0x00E20812  bsr.w remove; reload D1/D0; bra.b 0x00E20854 (the LIFO walk)
 *
 * The list is ordered by locks descending then state descending; a PCB
 * moves when its prev has strictly less priority than it (more locks, or
 * equal locks and higher state) or its next has strictly more.  Both moves
 * re-insert LIFO (ahead of equal-priority entries).  On m68k this is
 * proc1/sau2/ready_list.s; this C body is for other targets.
 */

#include "proc1/proc1_internal.h"

#if !defined(ARCH_M68K)

void proc1_$reorder_if_needed(proc1_t *pcb)
{
    uint32_t locks = pcb->resource_locks_held;      /* D1 */
    proc1_t *neighbour;                             /* A0 */

    /* 0x00E207DC */
    if (pcb != PROC1_$READY_PCB) {
        /* 0x00E207E2..0x00E207F6 */
        neighbour = pcb->prevp;
        if (locks > neighbour->resource_locks_held ||
            (locks == neighbour->resource_locks_held &&
             pcb->state > neighbour->state)) {
            /* 0x00E207F8 / 0x00E207FC */
            proc1_$remove_from_ready_list(pcb);
            proc1_$insert_into_ready_list(pcb);
            return;
        }
    }

    /* 0x00E207FE..0x00E20810 */
    neighbour = pcb->nextp;
    if (locks > neighbour->resource_locks_held) {
        return;
    }
    if (locks == neighbour->resource_locks_held &&
        pcb->state >= neighbour->state) {
        return;
    }

    /*
     * 0x00E20812..0x00E2081E: remove, reload D1/D0 and `bra.b 0x00E20854' -
     * the LIFO walk is resumed with A0 still holding the PCB's former next,
     * not restarted from PROC1_$READY_PCB.
     */
    proc1_$remove_from_ready_list(pcb);
    proc1_$insert_scan(pcb, neighbour);
}

#endif /* !ARCH_M68K */
