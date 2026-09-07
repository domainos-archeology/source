/*
 * PROC1_$RESUME - Resume a suspended process
 *
 * Resumes a process that was previously suspended.  If the process only had
 * a deferred suspend pending (but was never actually suspended), the
 * deferred flag is cleared instead.
 *
 * Parameters:
 *   pid      - Process ID to resume (1-64)
 *   status_p - Status return
 *
 * Status codes:
 *   status_$illegal_process_id     - Invalid PID (0 or > 64)
 *   status_$process_not_bound      - Process slot not in use
 *   status_$process_not_suspended  - Process was not suspended
 *
 * Original address: 0x00E1476E (140 bytes)
 *
 * Full instruction trace:
 *   00e1476e  link.w A6,-0x4
 *   00e14772  pea (A2)
 *   00e14774  move.w (0x8,A6),D0w        ; pid
 *   00e14778  movea.l (0xa,A6),A0        ; status_p
 *   00e1477c  beq.b 0x00e14784           ; pid == 0
 *   00e1477e  cmpi.w #0x40,D0w
 *   00e14782  bls.b 0x00e1478c
 *   00e14784  move.l #0xa0001,(A0)       ; status_$illegal_process_id
 *   00e1478a  bra.b 0x00e147f2           ; -> epilogue, IPL untouched
 *   00e1478c  move.w D0w,D1w
 *   00e1478e  movea.l #0xe1eacc,A1       ; PCBS
 *   00e14794  lsl.w #0x2,D1w
 *   00e14796  movea.l (0x0,A1,D1w),A2    ; pcb = PCBS[pid]
 *   00e1479a  btst.b #0x3,(0x55,A2)      ; PROC1_FLAG_BOUND
 *   00e147a0  bne.b 0x00e147aa
 *   00e147a2  move.l #0xa0005,(A0)       ; status_$process_not_bound
 *   00e147a8  bra.b 0x00e147f2           ; -> epilogue, IPL untouched
 *   00e147aa  clr.l (A0)                 ; status_$ok  (BEFORE the IPL raise)
 *   00e147ac  ori #0x700,SR              ; raise to IPL 7 (no SR saved)
 *   00e147b0  btst.b #0x1,(0x55,A2)      ; PROC1_FLAG_SUSPENDED (re-read)
 *   00e147b6  beq.b 0x00e147d8
 *   00e147b8  bclr.b #0x1,(0x55,A2)
 *   00e147be  btst.b #0x0,(0x55,A2)      ; PROC1_FLAG_WAITING (re-read)
 *   00e147c4  bne.b 0x00e147d0
 *   00e147c6  pea (A2)
 *   00e147c8  jsr 0x00e20820.l           ; PROC1_$ADD_READY(pcb)
 *   00e147ce  addq.w #0x4,SP
 *   00e147d0  jsr 0x00e20a18.l           ; PROC1_$DISPATCH()
 *   00e147d6  bra.b 0x00e147f2           ; -> epilogue WITHOUT touching the IPL
 *   00e147d8  btst.b #0x2,(0x55,A2)      ; PROC1_FLAG_DEFER_SUSP (re-read)
 *   00e147de  beq.b 0x00e147e8
 *   00e147e0  bclr.b #0x2,(0x55,A2)
 *   00e147e6  bra.b 0x00e147ee
 *   00e147e8  move.l #0xa0003,(A0)       ; status_$process_not_suspended
 *   00e147ee  andi #-0x701,SR            ; forced IPL 0
 *   00e147f2  movea.l (-0x8,A6),A2
 *   00e147f6  unlk A6
 *   00e147f8  rts
 *
 * Note the suspended path at 0x00E147D6 leaves the function at IPL 7:
 * PROC1_$DISPATCH is what lowers it again.  Only the two paths that fall
 * through 0x00E147EE force IPL 0 here.  Each flag test re-reads the byte at
 * PCB+0x55; nothing is cached across the IPL raise.
 */

#include "proc1/proc1_internal.h"

void PROC1_$RESUME(uint16_t pid, status_$t *status_p)
{
    proc1_t *pcb;

    /* 0x00E1477C / 0x00E1477E */
    if (pid == 0 || pid > 0x40) {
        *status_p = status_$illegal_process_id;
        return;
    }

    /* 0x00E1478E: PCBS is indexed by the raw pid (slot 0 is unused) */
    pcb = PCBS[pid];

    /* 0x00E1479A: btst.b #0x3,(0x55,A2) */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        *status_p = status_$process_not_bound;
        return;
    }

    /* 0x00E147AA */
    *status_p = status_$ok;

    /* 0x00E147AC: ori #0x700,SR -- no SR is saved */
    SET_IPL7();

    /* 0x00E147B0: btst.b #0x1,(0x55,A2) */
    if ((pcb->pri_max & PROC1_FLAG_SUSPENDED) != 0) {
        /* 0x00E147B8 */
        pcb->pri_max = (uint8_t)(pcb->pri_max & ~PROC1_FLAG_SUSPENDED);

        /* 0x00E147BE: not waiting on an eventcount -> back on the ready list */
        if ((pcb->pri_max & PROC1_FLAG_WAITING) == 0) {
            PROC1_$ADD_READY(pcb);
        }

        /* 0x00E147D0 */
        PROC1_$DISPATCH();

        /* 0x00E147D6: returns at IPL 7; PROC1_$DISPATCH lowers it */
        return;
    }

    /* 0x00E147D8: btst.b #0x2,(0x55,A2) */
    if ((pcb->pri_max & PROC1_FLAG_DEFER_SUSP) != 0) {
        /* 0x00E147E0 */
        pcb->pri_max = (uint8_t)(pcb->pri_max & ~PROC1_FLAG_DEFER_SUSP);
    } else {
        /* 0x00E147E8 */
        *status_p = status_$process_not_suspended;
    }

    /* 0x00E147EE: andi #-0x701,SR -- forced IPL 0, not an SR restore */
    SET_IPL0();
}
