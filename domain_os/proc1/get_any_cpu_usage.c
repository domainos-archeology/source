/*
 * PROC1_$GET_ANY_CPU_USAGE - Get CPU usage statistics for any process
 * Original address: 0x00e1543e (102 bytes)
 *
 * 0x00E1543E  link.w A6,-0x4 / movem.l {A3 A2},-(SP)
 * 0x00E15446  movea.l (0x8,A6),A2            ; argument 1: pid_ptr
 * 0x00E1544A  movea.l (0xc,A6),A3            ; argument 2: cpu_time_ret
 * 0x00E1544E  move.w (A2),D0w
 * 0x00E15450  beq.b 0x00E15458
 * 0x00E15452  cmpi.w #0x40,D0w / bls.b 0x00E15464
 * 0x00E15458  pea (-0x17a,PC)                ; 0x00E152E0 Illegal_process_id_err
 * 0x00E1545C  jsr CRASH_SYSTEM / addq.w #0x4,SP
 * 0x00E15464  move.w (A2),D0w                ; pid re-read from the caller's cell
 * 0x00E15466  movea.l #0xe1eacc,A0           ; PCBS
 * 0x00E1546C  lsl.w #0x2,D0w
 * 0x00E1546E  lea (0x0,A0,D0w*0x1),A1
 * 0x00E15472  movea.l (A1),A2                ; PCBS[pid]
 * 0x00E15474  move.l (0x4c,A2),(A3)          ; cpu_total
 * 0x00E15478  move.w (0x50,A2),(0x4,A3)      ; cpu_usage
 * 0x00E1547E  pea (0x4c,A2) / pea (A3)
 * 0x00E15484  jsr ADD48                      ; ADD48(cpu_time_ret, &pcb->cpu) -> doubled
 * 0x00E1548A  movea.l (0x10,A6),A1           ; argument 3
 * 0x00E1548E  move.l (0x60,A2),(A1)          ; field_60
 * 0x00E15492  movea.l (0x14,A6),A0           ; argument 4
 * 0x00E15496  move.l (0x64,A2),(A0)          ; field_64
 * 0x00E1549A  movem.l (-0xc,A6),{A2 A3} / unlk A6 / rts
 *
 * The 48-bit time is copied out and then ADD48ed with itself, i.e. the
 * caller receives twice the PCB's accumulated time - the same units
 * PROC1_$GET_CPUT produces with its `lsl.w #1 / roxl.l #1'.
 *
 * Parameters:
 *   pid_ptr      - pointer to the process id (word), 1..64
 *   cpu_time_ret - receives the doubled 48-bit CPU time (clock_t shaped)
 *   stat1_ret    - receives PCB+0x60
 *   stat2_ret    - receives PCB+0x64
 */

#include "proc1/proc1_internal.h"
#include "misc/misc.h"
#include "cal/cal.h"

void PROC1_$GET_ANY_CPU_USAGE(uint16_t *pid_ptr, void *cpu_time_ret,
                               uint32_t *stat1_ret, uint32_t *stat2_ret)
{
    clock_t *out = (clock_t *)cpu_time_ret;
    proc1_t *pcb;

    /* 0x00E1544E..0x00E15462 */
    if (*pid_ptr == 0 || *pid_ptr > 0x40) {
        CRASH_SYSTEM(&Illegal_process_id_err);
        /* the image continues here if CRASH_SYSTEM ever returned */
    }

    /* 0x00E15464..0x00E15478: the pid is re-read from the caller's cell */
    pcb = PCBS[*pid_ptr];
    out->high = pcb->cpu_total;
    out->low = pcb->cpu_usage;

    /*
     * 0x00E1547E..0x00E15484: cpu_total (0x4C) and cpu_usage (0x50) are
     * adjacent in the PCB, so &pcb->cpu_total is the six-byte clock ADD48
     * reads (proc1_t asserts the offsets).
     */
    ADD48(out, (clock_t *)&pcb->cpu_total);

    /* 0x00E1548A..0x00E15496 */
    *stat1_ret = pcb->field_60;
    *stat2_ret = pcb->field_64;
}
