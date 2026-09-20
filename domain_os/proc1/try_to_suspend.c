/*
 * PROC1_$TRY_TO_SUSPEND - Suspend a process now, or mark it for later
 * Original address: 0x00e1471c (82 bytes)
 *
 * Frame: (0x8,A6) pcb.  Called at IPL 7 by PROC1_$SUSPEND (0x00E14856),
 * PROC1_$UNBIND (0x00E14E86) and the lock-release tail (0x00E20EDA).
 *
 * 0x00E1471C  link.w A6,0x0 / pea (A2)
 * 0x00E14722  A2 = pcb
 * 0x00E14726  bset.b #2,(0x55,A2)                     DEFER_SUSP on
 * 0x00E1472C  D0 = PROC1_$INHIBIT_CHECK(pcb)          (`pea (A2)'; addq #4)
 * 0x00E14736  tst.b D0 / bmi exit                     inhibited: leave it
 *                                                     deferred
 * 0x00E1473A  btst.b #0,(0x55,A2) / bne 0x00E1474C    WAITING: not on the
 *                                                     ready list
 * 0x00E14742  PROC1_$REMOVE_READY(pcb)                (`pea (A2)'; addq #4)
 * 0x00E1474C  D0 = word (0x54,A2) & 0xFFFB | 0x0002   DEFER_SUSP off,
 *             (0x54,A2) = D0                          SUSPENDED on - a WORD
 *                                                     store over pri_min:
 *                                                     pri_max
 * 0x00E1475A  ADVANCE(&PROC1_$SUSPEND_EC)             (`move.l #0xe205f6';
 *             the internal entry at 0x00E20728, i.e. without the IPL
 *             bracket and without a dispatch; no cleanup: unlk)
 * 0x00E14766  movea.l (-0x4,A6),A2 / unlk / rts
 *
 * Parameters:
 *   pcb - the process to suspend
 */

#include "proc1/proc1_internal.h"
#include "ec/ec.h"

void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb)
{
    uint16_t flags;

    /* 0x00E14726: bset.b #0x2,(0x55,A2) */
    pcb->pri_max = (uint8_t)(pcb->pri_max | PROC1_FLAG_DEFER_SUSP);

    /* 0x00E1472C / 0x00E14736: tst.b D0b / bmi */
    if (PROC1_$INHIBIT_CHECK(pcb) < 0) {
        return;
    }

    /* 0x00E1473A: btst.b #0x0,(0x55,A2) */
    if ((pcb->pri_max & PROC1_FLAG_WAITING) == 0) {
        /* 0x00E14742 */
        PROC1_$REMOVE_READY(pcb);
    }

    /* 0x00E1474C..0x00E14756: moveq #-5 / and.w (0x54,A2) / ori.w #2 / move.w */
    flags = (uint16_t)(((uint16_t)pcb->pri_min << 8) | pcb->pri_max);
    flags = (uint16_t)((flags & 0xFFFB) | 0x0002);
    pcb->pri_min = (uint8_t)(flags >> 8);
    pcb->pri_max = (uint8_t)(flags & 0xFF);

    /* 0x00E1475A / 0x00E14760 */
    ADVANCE(&PROC1_$SUSPEND_EC);
}
