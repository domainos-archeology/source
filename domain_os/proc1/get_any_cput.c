/*
 * PROC1_$GET_ANY_CPUT - Get the accumulated CPU time of any process
 * Original address: 0x00e153f8 (70 bytes)
 *
 * 0x00E153F8  link.w A6,0x0 / movem.l {A2 D2},-(SP)
 * 0x00E15400  move.w (0xc,A6),D2w            ; argument 2: pid (word)
 * 0x00E15404  beq.b 0x00E1540C
 * 0x00E15406  cmpi.w #0x40,D2w / bls.b 0x00E15416
 * 0x00E1540C  pea (-0x12e,PC)                ; 0x00E152E0 Illegal_process_id_err
 * 0x00E15410  jsr CRASH_SYSTEM               ; (never pops; falls through)
 * 0x00E15416  move.w D2w,D0w
 * 0x00E15418  movea.l #0xe1eacc,A0           ; PCBS
 * 0x00E1541E  lsl.w #0x2,D0w
 * 0x00E15420  movea.l (0x8,A6),A2            ; argument 1: cpu_time_ret
 * 0x00E15424  lea (0x0,A0,D0w*0x1),A1
 * 0x00E15428  movea.l (A1),A0                ; PCBS[pid]
 * 0x00E1542A  move.l (0x4c,A0),(A2)          ; cpu_total
 * 0x00E1542E  move.w (0x50,A0),(0x4,A2)      ; cpu_usage
 * 0x00E15434  movem.l (-0x8,A6),{D2 A2} / unlk A6 / rts
 *
 * Unlike PROC1_$GET_CPUT this reads the PCB fields as they stand: no virtual
 * timer correction and no doubling.  No caller in the image.
 *
 * Parameters:
 *   cpu_time_ret - receives the 48-bit CPU time
 *   pid          - process id, 1..64
 */

#include "proc1/proc1_internal.h"
#include "misc/misc.h"

void PROC1_$GET_ANY_CPUT(clock_t *cpu_time_ret, uint16_t pid)
{
    proc1_t *pcb;

    /* 0x00E15404..0x00E15410 */
    if (pid == 0 || pid > 0x40) {
        CRASH_SYSTEM(&Illegal_process_id_err);
        /* the image continues here if CRASH_SYSTEM ever returned */
    }

    /* 0x00E15416..0x00E1542E */
    pcb = PCBS[pid];
    cpu_time_ret->high = pcb->cpu_total;
    cpu_time_ret->low = pcb->cpu_usage;
}
