/*
 * PROC1_$SET_VT - Set a process's virtual (CPU-time) timer
 * Original address: 0x00e1495c (170 bytes)
 *
 * Frame: (0x8,A6) pid (word), (0xA,A6) vt (a 6-byte clock), (0xE,A6) status.
 * Locals: (-0x8,A6) a 6-byte clock {0, elapsed}.
 *
 * 0x00E1495C  link.w A6,-0x18 / movem.l D2/D3/A2,-(SP)
 * 0x00E14964  D0 = pid; A0 = vt; A1 = status; beq / cmpi.w #0x40 / bls
 * 0x00E14978  *status = 0x000A0001; bra exit
 * 0x00E14980  A2 = PCBS[pid]; btst.b #3,(0x55,A2) / bne
 * 0x00E14998  *status = 0x000A0005; bra exit
 * 0x00E149A0  tst.l (A0) / beq 0x00E149AA
 * 0x00E149A4  D2 = -1                                  high word set: saturate
 * 0x00E149AA  D2 = (0x4,A0)                            else the low word
 * 0x00E149AE  cmpa.l PROC1_$CURRENT_PCB,A2 / bne 0x00E149F8
 * 0x00E149B6  D3 = SR; SR = 0x2700                     true save
 * 0x00E149BC  clr.l (-0x8,A6)
 * 0x00E149C0  D0 = TIME_$VT_TIMER()
 * 0x00E149C6  D1 = (0x48,A2) - D0; (-0x4,A6) = D1      elapsed = vtimer - now
 * 0x00E149D0  ADD48(&pcb->cpu_total, &elapsed)         (addq #8)
 * 0x00E149E0  (0x48,A2) = D2                           vtimer = new value
 * 0x00E149E4  TIME_$WRT_TIMER(&PROC1_$VT_TIMER_DATA, &pcb->vtimer)
 *             (`pea (0x48,A2)' then `pea (0x1c,PC)' -> 0x00E14A06, the
 *             constant word 2 = the VT channel; no cleanup: unlk)
 * 0x00E149F2  D1 = SR; SR = D3                         restore
 * 0x00E149F6  bra exit
 * 0x00E149F8  (0x48,A2) = D2                           not current: just store
 * 0x00E149FC  movem.l / unlk / rts
 *
 * Quirk reproduced: the status is written only on the two failure paths;
 * a successful call leaves *status untouched.  Only caller:
 * TIME_$Q_INIT_QUEUE (0x00E16C22).
 *
 * Parameters:
 *   pid        - the process
 *   vt         - the new timer value as a 48-bit clock; any non-zero high
 *                longword saturates the 16-bit timer at 0xFFFF
 *   status_ret - written on failure only
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"
#include "cal/cal.h"

void PROC1_$SET_VT(uint16_t pid, clock_t *vt, status_$t *status_ret)
{
    proc1_t *pcb;               /* A2 */
    uint16_t new_vtimer;        /* D2 */
    clock_t elapsed;            /* (-0x8,A6) */
    uint16_t saved_sr;          /* D3 */
    uint16_t now;               /* D0 */

    /* 0x00E14970 / 0x00E14972 */
    if (pid == 0 || pid > 0x40) {
        /* 0x00E14978 */
        *status_ret = status_$illegal_process_id;
        return;
    }

    /* 0x00E14980..0x00E1498E */
    pcb = PCBS[pid];

    /* 0x00E14990: btst.b #0x3,(0x55,A2) */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        /* 0x00E14998 */
        *status_ret = status_$process_not_bound;
        return;
    }

    /* 0x00E149A0..0x00E149AA */
    if (vt->high != 0) {
        new_vtimer = 0xFFFF;
    } else {
        new_vtimer = vt->low;
    }

    /* 0x00E149AE */
    if (pcb == PROC1_$CURRENT_PCB) {
        /* 0x00E149B6 / 0x00E149B8: move SR,D3 / move #0x2700,SR */
        DISABLE_INTERRUPTS(saved_sr);

        /* 0x00E149BC */
        elapsed.high = 0;

        /* 0x00E149C0..0x00E149CC */
        now = TIME_$VT_TIMER();
        elapsed.low = (uint16_t)((uint16_t)pcb->vtimer - now);

        /* 0x00E149D0..0x00E149DE: ADD48(&pcb->cpu_total, &elapsed) */
        ADD48((clock_t *)&pcb->cpu_total, &elapsed);

        /* 0x00E149E0 */
        pcb->vtimer = (int16_t)new_vtimer;

        /* 0x00E149E4..0x00E149EC */
        TIME_$WRT_TIMER((uint16_t *)&PROC1_$VT_TIMER_DATA, (uint16_t *)&pcb->vtimer);

        /* 0x00E149F2 / 0x00E149F4: move D3,SR */
        ENABLE_INTERRUPTS(saved_sr);
        return;
    }

    /* 0x00E149F8 */
    pcb->vtimer = (int16_t)new_vtimer;
}
