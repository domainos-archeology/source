/*
 * PROC1_$TS_END_CALLBACK - A process's timeslice has run out
 * Original address: 0x00e14a70 (162 bytes)
 *
 * Fired by TIME_$Q_SCAN_QUEUE from the process's virtual-timer queue with
 * the direct-callback argument shape (time/time.h): *arg is the address
 * of the time_queue_elem_t, whose callback_arg PROC1_$INIT_TS_TIMER set to
 * the pid - the code reads its LOW word at element offset 0xA.
 *
 * Frame: (0x8,A6) arg.  Locals: (-0x8,A6) the new timeslice word.
 *
 * 0x00E14A70  link.w A6,-0x14 / movem.l D2/D3/A2/A3,-(SP)
 * 0x00E14A78  A0 = arg; A1 = (A0); D0 = (0xA,A1)      pid
 * 0x00E14A82  D2 = PCBS[pid]
 * 0x00E14A90  D3 = SR; SR = 0x2700                    true save
 * 0x00E14A96  A3 = A2 = pcb
 * 0x00E14A9A  cmpi.w #2,D0 / bne 0x00E14AA8
 * 0x00E14AA0  timeslice = -1; bra 0x00E14AF8          pid 2 never ages
 * 0x00E14AA8  subq.w #1,(0x52,A2)                     state--
 * 0x00E14AAC  D1 = (0x56,A3); cmp.w (0x52,A2),D1 / ble
 * 0x00E14AB6  (0x52,A2) = D1                          state = max(state, inh_count)
 * 0x00E14ABA  tst.l (0x40,A3) / beq 0x00E14AD2
 * 0x00E14AC0  PROC1_$REORDER_READY(pcb) (`pea (A3)'; addq #4)   holds locks
 * 0x00E14ACA  bset.b #4,(0x55,A3); bra 0x00E14AE6                 boosted flag
 * 0x00E14AD2  PROC1_$REMOVE_READY(pcb); PROC1_$ADD_READY(pcb)     no locks:
 *             (`pea (A3)' / addq #4 each)                          FIFO re-add
 * 0x00E14AE6  D1 = (0x52,A2) * 2; timeslice = (-0x2,A1,D1) with A1 =
 *             0xE205D2                                PROC1_$TSVV[state - 1]
 * 0x00E14AF8  D0 = SR; SR = D3                        restore
 * 0x00E14AFC  PROC1_$SET_TS(pcb, timeslice)           (result slot; no
 *                                                     cleanup: unlk)
 * 0x00E14B08  movem.l / unlk / rts
 *
 * Parameters:
 *   arg - the callback cell: a pointer to the element's address
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"

void PROC1_$TS_END_CALLBACK(void *arg)
{
    time_queue_elem_t *elem;    /* A1 */
    proc1_t *pcb;               /* A2 = A3 */
    uint16_t pid;               /* D0 */
    int16_t d1;
    int16_t timeslice;          /* (-0x8,A6) */
    uint16_t saved_sr;          /* D3 */

    /* 0x00E14A78..0x00E14A7E: A1 = *arg; D0 = word (0xA,A1) */
    elem = (time_queue_elem_t *)ARCH_VA_TO_PTR(*(uint32_t *)arg);
    pid = (uint16_t)(elem->callback_arg & 0xFFFFu);

    /* 0x00E14A82..0x00E14A8C */
    pcb = PCBS[pid];

    /* 0x00E14A90 / 0x00E14A92: move SR,D3 / move #0x2700,SR */
    DISABLE_INTERRUPTS(saved_sr);

    /* 0x00E14A9A */
    if (pid == 2) {
        /* 0x00E14AA0 */
        timeslice = -1;
    } else {
        /* 0x00E14AA8 */
        pcb->state--;

        /* 0x00E14AAC..0x00E14AB6: signed compare (ble) */
        d1 = (int16_t)pcb->inh_count;
        if (d1 > (int16_t)pcb->state) {
            pcb->state = (uint16_t)d1;
        }

        /* 0x00E14ABA: tst.l (0x40,A3) */
        if (pcb->resource_locks_held != 0) {
            /* 0x00E14AC0 / 0x00E14ACA */
            PROC1_$REORDER_READY(pcb);
            pcb->pri_max = (uint8_t)(pcb->pri_max | 0x10);
        } else {
            /* 0x00E14AD2 / 0x00E14ADC */
            PROC1_$REMOVE_READY(pcb);
            PROC1_$ADD_READY(pcb);
        }

        /* 0x00E14AE6..0x00E14AF2: PROC1_$TSVV is 1-based by state */
        timeslice = PROC1_$TSVV[pcb->state - 1];
    }

    /* 0x00E14AF8 / 0x00E14AFA: move D3,SR */
    ENABLE_INTERRUPTS(saved_sr);

    /* 0x00E14AFC..0x00E14B04 */
    PROC1_$SET_TS(pcb, timeslice);
}
