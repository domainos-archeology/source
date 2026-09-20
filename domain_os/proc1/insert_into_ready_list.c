/*
 * proc1_$insert_into_ready_list - LIFO priority-ordered ready-list insertion
 * Original address: 0x00e20844 (56 bytes including the shared tail)
 *
 * Register convention: A1 = pcb.  Reached by `bra.b' from
 * proc1_$reorder_if_needed (0x00E207FC, and 0x00E2081E into the walk with
 * D0/D1 preloaded) and `bsr.w' from ADVANCE_INT (0x00E207B2).  It has no
 * stack gate and no C caller on m68k.
 *
 * 0x00E20844  D1 = (0x40,A1)                 pcb->resource_locks_held
 * 0x00E20848  D0 = (0x52,A1)                 pcb->state
 * 0x00E2084C  movea.l (-0x1c14,PC),A0        A0 = PROC1_$READY_PCB (0xE1EC3A)
 * 0x00E20850  bra.b 0x00E20854
 * 0x00E20852  A0 = (A0)
 * 0x00E20854  cmp.l (0x40,A0),D1 / bhi 0x00E20862     more locks: insert here
 * 0x00E2085A  bne.b 0x00E20852               fewer locks: keep walking
 * 0x00E2085C  cmp.w (0x52,A0),D0 / bcs 0x00E20852     state < : keep walking
 *             (fall through: state >= pos->state inserts BEFORE pos, so an
 *             equal-priority newcomer goes ahead of its peers - LIFO; the
 *             FIFO twin proc1_$add_ready_body uses `bls' here)
 * 0x00E20862  shared tail: link pcb ahead of pos, PROC1_$READY_COUNT++, rts
 *
 * Both compares are unsigned.  On m68k this is proc1/sau2/ready_list.s;
 * this C body is for other targets.
 */

#include "proc1/proc1_internal.h"

#if !defined(ARCH_M68K)

/*
 * proc1_$insert_scan - the LIFO walk and shared tail from 0x00E20854
 *
 * The image enters here twice: from proc1_$insert_into_ready_list itself
 * (A0 = PROC1_$READY_PCB, 0x00E2084C) and from proc1_$reorder_if_needed's
 * second move (`bra.b 0x00E20854' at 0x00E2081E, A0 = the PCB's former
 * next).  D1/D0 are the PCB's locks/state in both cases.
 */
void proc1_$insert_scan(proc1_t *pcb, proc1_t *pos)
{
    proc1_t *prev;                                  /* D0 at 0x00E20864 */
    uint32_t locks = pcb->resource_locks_held;      /* D1 */
    uint16_t state = pcb->state;                    /* D0.w */

    /* 0x00E20854..0x00E20860 */
    for (;;) {
        /* 0x00E20854: cmp.l (0x40,A0),D1 / bhi */
        if (locks > pos->resource_locks_held) {
            break;
        }
        /* 0x00E2085A: bne */
        if (locks != pos->resource_locks_held) {
            pos = pos->nextp;
            continue;
        }
        /* 0x00E2085C: cmp.w (0x52,A0),D0w / bcs */
        if (state < pos->state) {
            pos = pos->nextp;
            continue;
        }
        break;
    }

    /* 0x00E20862..0x00E20872 */
    pcb->nextp = pos;
    prev = pos->prevp;
    pcb->prevp = prev;
    pos->prevp = pcb;
    prev->nextp = pcb;

    /* 0x00E20874 */
    PROC1_$READY_COUNT++;
}

void proc1_$insert_into_ready_list(proc1_t *pcb)
{
    /* 0x00E20844..0x00E2084C: D1/D0 from the PCB, A0 = PROC1_$READY_PCB */
    proc1_$insert_scan(pcb, PROC1_$READY_PCB);
}

#endif /* !ARCH_M68K */
