/*
 * PROC1_$INIT_TS_TIMER - Arm a process's timeslice timer
 * Original address: 0x00e14b12 (168 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8 (the PROC1_ data
 * block).  A2 = A5 + pid*0x1C (0x00E14B24..0x00E14B30 computes pid*32 -
 * pid*4), and the element used is (0x14,A2): PROC1_$DATA.ts_elem[pid].elem.
 * The element's fields therefore appear at A2 + 0x14 + field:
 * (0x18,A2) callback, (0x1C,A2) callback_arg, (0x20,A2)/(0x24,A2) expire,
 * (0x26,A2) flags.
 *
 * Locals: (-0x18,A6) a 6-byte clock {0, 0xFFFF}, (-0x10,A6) status,
 * (-0xC,A6) the PCB's CPU time as a 6-byte clock.
 *
 * 0x00E14B12  link.w A6,-0x1c / movem.l D2/A2/A5,-(SP) / lea A5
 * 0x00E14B20  D2 = pid; A2 = A5 + pid*0x1C
 * 0x00E14B34  elem.flags = 0
 * 0x00E14B38  A0 = PCBS[pid]
 * 0x00E14B48  now = {pcb->cpu_total, pcb->cpu_usage}
 * 0x00E14B54  delta = {0, 0xFFFF}
 * 0x00E14B5E  elem.expire = now
 * 0x00E14B6A  ADD48(&elem.expire, &delta)          (addq #8)
 * 0x00E14B7A  elem.callback = PROC1_$TS_END_CALLBACK (0x00E14A70)
 * 0x00E14B82  elem.callback_arg = (uint32_t)pid
 * 0x00E14B8A  TIME_$Q_ENTER_ELEM(&TIME_$VTQ[pid - 1], &now, &elem, &status)
 *             (A1 = 0xE2A4A0; D1 = pid*4 + pid*8 = pid*12; `pea
 *             (-0xc,A1,D1w)'; no cleanup: unlk)
 * 0x00E14BB0  movem.l / unlk / rts
 *
 * Callers push a Pascal result slot (`subq.l #2,SP' at 0x00E14E08,
 * 0x00E2F9E6, 0x00E2F9F4) but the routine never writes D0; the status is
 * never read.  The expiry is the process's CPU time plus 0xFFFF ticks
 * (the initial timeslice); PROC1_$SET_TS re-arms it later.
 *
 * Parameters:
 *   pid - the process whose timer to arm (1..64)
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"
#include "cal/cal.h"

/* 0x00E14B54 / 0x00E14B58: the initial timeslice, as a 48-bit clock */
#define PROC1_TS_INITIAL_HIGH   0x00000000u
#define PROC1_TS_INITIAL_LOW    0xFFFFu

void PROC1_$INIT_TS_TIMER(uint16_t pid)
{
    time_queue_elem_t *elem;    /* (0x14,A2) */
    proc1_t *pcb;               /* A0 */
    clock_t delta;              /* (-0x18,A6) */
    status_$t status;           /* (-0x10,A6) */
    clock_t now;                /* (-0xC,A6) */
    clock_t expire;             /* the 6 bytes at (0x20,A2) */

    /* 0x00E14B24..0x00E14B30 */
    elem = &PROC1_$DATA.ts_elem[pid].elem;

    /* 0x00E14B34 */
    elem->flags = 0;

    /* 0x00E14B38..0x00E14B4E */
    pcb = PCBS[pid];
    now.high = pcb->cpu_total;
    now.low = pcb->cpu_usage;

    /* 0x00E14B54 / 0x00E14B58 */
    delta.high = PROC1_TS_INITIAL_HIGH;
    delta.low = PROC1_TS_INITIAL_LOW;

    /* 0x00E14B5E / 0x00E14B64 */
    expire = now;

    /* 0x00E14B6A..0x00E14B72: ADD48 on the element's expire pair in place */
    ADD48(&expire, &delta);
    elem->expire_high = expire.high;
    elem->expire_low = expire.low;

    /* 0x00E14B7A */
    elem->callback = (uint32_t)(uintptr_t)PROC1_$TS_END_CALLBACK;

    /* 0x00E14B82 / 0x00E14B86: zero-extended pid */
    elem->callback_arg = (uint32_t)pid;

    /* 0x00E14B8A..0x00E14BAA: queue element pid-1 of the 12-byte VTQ array */
    TIME_$Q_ENTER_ELEM(&TIME_$VTQ[pid - 1], &now, elem, &status);
}
