/*
 * PROC1_$GET_INFO - Report a process's flags, CPU time and user registers
 * Original address: 0x00e14f52 (200 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8 (the PROC1_ data
 * block); OS_STACK_BASE is (0x730,A5 + pid*4).  PCBS is walked from
 * 0xE1EACC (`movea.l #0xe1eacc,A4').
 *
 * Frame: (0x8,A6) pid (pointer to a word), (0xC,A6) info, (0x10,A6) status.
 *
 * 0x00E14F52  link.w A6,-0x10 / movem.l D2/D3/A2-A5,-(SP) / lea A5
 * 0x00E14F60  A2 = info; A0 = status; D2 = *pid
 * 0x00E14F6E  *status = 0
 * 0x00E14F70  tst.w D2 / beq bad; cmpi.w #0x40 / bhi bad
 * 0x00E14F7A  D3 = pid*4; A3 = PCBS[pid]; cmpa.w #0,A3 / bne ok
 * 0x00E14F8E  *status = 0x000A0001; bra exit               (bad)
 * 0x00E14F96  btst.b #3,(0x55,A3) / bne bound
 * 0x00E14F9E  *status = 0x000A0005; bra exit
 * 0x00E14FA6  info->flags   = word (0x54,A3)                pri_min:pri_max
 * 0x00E14FAA  A4 = &pcb->cpu_total; (0x10,A2) = (A4)+;     8-byte copy of
 *             (0x14,A2) = (A4)+                             0x4C..0x53, i.e.
 *                                                           cpu_total,
 *                                                           cpu_usage, state
 * 0x00E14FB6  ADD48(&info->cpu.time, &pcb->cpu_total)      (addq #8): the
 *                                                           time is doubled
 * 0x00E14FC6  cmpa.l PROC1_$CURRENT_PCB,A3 / beq exit
 * 0x00E14FCE  A3 = A5 + pid*4; tst.l (0x730,A3) / beq clear
 * 0x00E14FD8  PROC1_$GET_INFO_INT(pid, stack - 0x1000, stack,
 *                                 &info->usr, &info->upc,
 *                                 &info->usb, &info->usp)   (result slot;
 *                                                           no cleanup: unlk)
 * 0x00E15002  bra exit
 * 0x00E15004  clear (0x2,A2)..(0xF,A2): usr, upc, usp, usb (clr.l x3, clr.w)
 * 0x00E15010  movem.l / unlk / rts
 *
 * Quirks reproduced:
 *   - the 8-byte block copy drags the PCB's state word into info+0x16;
 *   - the CPU time returned is PCB time + PCB time (ADD48 onto the copy);
 *   - for the current process the register fields are left untouched;
 *   - the stack base handed to GET_INFO_INT is the OS stack top minus
 *     0x1000 regardless of the stack's real size.
 *
 * Parameters:
 *   pidp       - pointer to the pid
 *   info_ret   - the 0x18-byte proc1_$info_t
 *   status_ret - status return
 */

#include "proc1/proc1_internal.h"
#include "cal/cal.h"

/* 0x00E14FF2: `subi.l #0x1000,D0' - the assumed OS stack size */
#define PROC1_INFO_OS_STACK_SIZE 0x1000u

void PROC1_$GET_INFO(int16_t *pidp, proc1_$info_t *info_ret, status_$t *status_ret)
{
    uint16_t pid;               /* D2 */
    proc1_t *pcb;               /* A3 */
    void *stack;                /* (0x730,A3) */

    /* 0x00E14F6C / 0x00E14F6E */
    pid = (uint16_t)*pidp;
    *status_ret = status_$ok;

    /* 0x00E14F70..0x00E14F78 */
    if (pid == 0 || pid > 0x40) {
        goto illegal_pid;
    }

    /* 0x00E14F7A..0x00E14F8C */
    pcb = PCBS[pid];
    if (pcb == NULL) {
        goto illegal_pid;
    }

    /* 0x00E14F96: btst.b #0x3,(0x55,A3) */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        /* 0x00E14F9E */
        *status_ret = status_$process_not_bound;
        return;
    }

    /* 0x00E14FA6: the pri_min:pri_max word */
    info_ret->flags = (uint16_t)(((uint16_t)pcb->pri_min << 8) | pcb->pri_max);

    /* 0x00E14FAA..0x00E14FB2: two longwords from PCB+0x4C */
    info_ret->cpu.time.high = pcb->cpu_total;
    info_ret->cpu.time.low = pcb->cpu_usage;
    info_ret->cpu.state = pcb->state;

    /* 0x00E14FB6..0x00E14FBE: ADD48(&info->cpu.time, &pcb->cpu_total) */
    ADD48(&info_ret->cpu.time, (clock_t *)&pcb->cpu_total);

    /* 0x00E14FC6 */
    if (pcb == PROC1_$CURRENT_PCB) {
        return;
    }

    /* 0x00E14FCE / 0x00E14FD2 */
    stack = ARCH_VA_TO_PTR(PROC1_$DATA.os_stack_base[pid]);
    if (stack != NULL) {
        /* 0x00E14FD8..0x00E14FFC */
        PROC1_$GET_INFO_INT(pid,
                            ARCH_VA_TO_PTR(ARCH_PTR_TO_VA(stack) - PROC1_INFO_OS_STACK_SIZE),
                            stack,
                            &info_ret->usr,
                            &info_ret->upc,
                            &info_ret->usb,
                            &info_ret->usp);
        return;
    }

    /* 0x00E15004..0x00E1500E: 14 bytes from info+2 */
    info_ret->usr = 0;
    info_ret->upc = 0;
    info_ret->usp = 0;
    info_ret->usb = 0;
    return;

illegal_pid:
    /* 0x00E14F8E */
    *status_ret = status_$illegal_process_id;
}
