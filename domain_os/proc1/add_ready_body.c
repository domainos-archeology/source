/*
 * proc1_$add_ready_body - FIFO priority-ordered ready-list insertion
 * Original address: 0x00e20824 (32 bytes + the shared 24-byte tail)
 *
 * The body of PROC1_$ADD_READY (0x00E20820, `movea.l (0x4,SP),A1' falls
 * into it).  Register convention: A1 = pcb.  `bsr' callers: the ML_$UNLOCK /
 * ML_$EXCLUSION_STOP / PROC1_$INHIBIT_END release tail at 0x00E20ECC, and
 * the `bsr.b' at 0x00E20888 inside an unnamed 24-byte routine at
 * 0x00E2087C (no SAU2 map symbol, no Ghidra function or xrefs: A1 =
 * PROC1_$CURRENT_PCB, IPL 7, remove + re-add, PROC1_$DISPATCH_INT, forced
 * IPL 0).  That routine is not emitted by the tree - see bead source-268i.
 * PROC1_$INT_EXIT is a different routine at 0x00E208FE.
 *
 * 0x00E20824  D1 = (0x40,A1)                 pcb->resource_locks_held
 * 0x00E20828  D0 = (0x52,A1)                 pcb->state
 * 0x00E2082C  movea.l (-0x1bf4,PC),A0        A0 = PROC1_$READY_PCB (0xE1EC3A)
 * 0x00E20830  bra.b 0x00E20834
 * 0x00E20832  A0 = (A0)                      next
 * 0x00E20834  cmp.l (0x40,A0),D1 / bhi 0x00E20862     more locks: insert here
 * 0x00E2083A  bne.b 0x00E20832               fewer locks: keep walking
 * 0x00E2083C  cmp.w (0x52,A0),D0 / bls 0x00E20832     state <= : keep walking
 * 0x00E20842  bra.b 0x00E20862               state > : insert here
 *
 * The insertion tail at 0x00E20862 is shared with
 * proc1_$insert_into_ready_list (0x00E20844), whose only difference is the
 * `bcs' at 0x00E20860 (strictly lower state walks on, so equal-priority
 * entries are inserted BEFORE - LIFO - rather than after - FIFO - here):
 *
 * 0x00E20862  (A1) = A0                      pcb->nextp = pos
 * 0x00E20864  D0 = (0x4,A0); (0x4,A1) = D0   pcb->prevp = pos->prevp
 * 0x00E2086C  (0x4,A0) = A1                  pos->prevp = pcb
 * 0x00E20870  A0 = D0; (A0) = A1             prev->nextp = pcb
 * 0x00E20874  addq.w #1,(0x00e1ebd0).l       PROC1_$READY_COUNT++
 * 0x00E2087A  rts
 *
 * Both compares are unsigned (bhi/bls).  The walk terminates at the
 * sentinel PCB whose state (0x08) is below every real process.
 *
 * On m68k this is the PROC1_$ADD_READY block of proc1/sau2/ready_list.s,
 * which keeps the register convention for the assembly callers
 * (proc1_$add_ready_body_int) and aliases proc1_$add_ready_body to the
 * stack-argument gate so C callers still work; this C body is the same
 * walk for other targets.
 */

#include "proc1/proc1_internal.h"

#if !defined(ARCH_M68K)

void proc1_$add_ready_body(proc1_t *pcb)
{
    proc1_t *pos;                                   /* A0 */
    proc1_t *prev;                                  /* D0 at 0x00E20864 */
    uint32_t locks = pcb->resource_locks_held;      /* D1 */
    uint16_t state = pcb->state;                    /* D0.w */

    /* 0x00E2082C */
    pos = PROC1_$READY_PCB;

    /* 0x00E20834..0x00E20842 */
    for (;;) {
        /* 0x00E20834: cmp.l (0x40,A0),D1 / bhi */
        if (locks > pos->resource_locks_held) {
            break;
        }
        /* 0x00E2083A: bne */
        if (locks != pos->resource_locks_held) {
            pos = pos->nextp;
            continue;
        }
        /* 0x00E2083C: cmp.w (0x52,A0),D0w / bls */
        if (state <= pos->state) {
            pos = pos->nextp;
            continue;
        }
        /* 0x00E20842 */
        break;
    }

    /* 0x00E20862..0x00E20872: link pcb ahead of pos */
    pcb->nextp = pos;
    prev = pos->prevp;
    pcb->prevp = prev;
    pos->prevp = pcb;
    prev->nextp = pcb;

    /* 0x00E20874 */
    PROC1_$READY_COUNT++;
}

#endif /* !ARCH_M68K */
