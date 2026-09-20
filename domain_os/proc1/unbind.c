/*
 * PROC1_$UNBIND - Suspend a process and release its PCB
 * Original address: 0x00e14e24 (298 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8; OS_STACK_BASE is
 * (0x730,A5 + pid*4).  PCBS is 0xE1EACC, PROC1_$CURRENT_PCB 0xE1EAC8,
 * PROC1_$SUSPEND_EC 0xE205F6, TIME_$VTQ 0xE2A4A0.
 *
 * Frame: (0x8,A6) pid (word), (0xA,A6) status.
 *
 * 0x00E14E24  link.w A6,-0xc / movem.l D2-D5/A2-A5,-(SP) / lea A5
 * 0x00E14E32  D2 = pid; A2 = status; beq / cmpi.w #0x40 / bls
 * 0x00E14E42  *status = 0x000A0001; bra exit
 * 0x00E14E4C  D3 = pid*4; A3 = PCBS[pid]
 * 0x00E14E5A  btst.b #3,(0x55,A3) / bne
 * 0x00E14E62  *status = 0x000A0005; bra exit
 * 0x00E14E6C  cmpa.l PROC1_$CURRENT_PCB,A3 / bne 0x00E14EA2
 *   -- unbinding ourselves --
 * 0x00E14E74  PMAP_$PURGE_WS(pid, 0)                     (addq #4)
 * 0x00E14E80  ori #0x700,SR                              raised, not lowered
 * 0x00E14E84  PROC1_$TRY_TO_SUSPEND(pcb)                 (addq #4)
 * 0x00E14E8C  btst.b #1,(0x55,A3) / bne 0x00E14F04       suspended: go on
 * 0x00E14E94  CRASH_SYSTEM(&0x00E14F4E)                  (`pea (0xb8,PC)';
 *                                                         cell 00 0a 00 0a)
 * 0x00E14EA0  bra 0x00E14F04
 *   -- unbinding another process --
 * 0x00E14EA2  btst.b #1,(0x55,A3) / bne 0x00E14EF4       already suspended
 * 0x00E14EAA  D4 = PROC1_$SUSPEND_EC.value               read BEFORE suspend
 * 0x00E14EB0  D0 = PROC1_$SUSPEND(pid, status)           (result slot; addq #8)
 * 0x00E14EBC  A4 = 0; D5 = D4 + 1; bra 0x00E14EF0
 * 0x00E14ECA  EC_$WAIT({&SUSPEND_EC, 0, 0}, {D5, 0, 0})  (lea (0x18,SP),SP)
 * 0x00E14EE4  D0 = PROC1_$SUSPENDP(pid, status)          (result slot; addq #8)
 * 0x00E14EF0  tst.b D0 / bpl 0x00E14ECA                  loop until TRUE
 * 0x00E14EF4  PMAP_$PURGE_WS(pid, 0)                     (addq #4)
 * 0x00E14F00  ori #0x700,SR                              raised, not lowered
 *   -- common tail, IPL 7 --
 * 0x00E14F04  TIME_$Q_FLUSH_QUEUE(&TIME_$VTQ[pid - 1])   (D4 = pid*12; addq #4)
 * 0x00E14F20  bclr.b #3,(0x55,A3)                        BOUND off
 * 0x00E14F26  PROC1_$FREE_STACK(OS_STACK_BASE[pid])      (addq #4)
 * 0x00E14F34  PROC1_$SET_TYPE(pid, 0)                    (addq #4)
 * 0x00E14F3E  PROC1_$DISPATCH()                          forces IPL 0
 * 0x00E14F44  movem.l / unlk / rts
 *
 * Quirks reproduced:
 *   - the wait value is SUSPEND_EC.value + 1 as sampled before
 *     PROC1_$SUSPEND, and is not re-sampled inside the loop;
 *   - the status the loop's SUSPEND / SUSPENDP calls write is what the
 *     caller sees; nothing sets status_$ok on the success path except
 *     those callees;
 *   - the self-unbind path does not return: PROC1_$DISPATCH switches away
 *     from a suspended, unbound process.
 *
 * Parameters:
 *   pid        - the process to unbind
 *   status_ret - status return
 */

#include "proc1/proc1_internal.h"
#include "pmap/pmap.h"
#include "time/time.h"
#include "ec/ec.h"
#include "misc/misc.h"

/*
 * The status cell at 0x00E14F4E (`pea (0xb8,PC)' at 0x00E14E94):
 * image bytes 00 0a 00 0a, status_$process_not_suspendable.
 */
static const status_$t proc1_$not_suspendable_00e14f4e = status_$process_not_suspendable;

void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret)
{
    proc1_t *pcb;               /* A3 */
    int32_t ec_value;           /* D4 */
    int32_t wait_value;         /* D5 */
    int8_t suspended;           /* D0.b */

    /* 0x00E14E3A / 0x00E14E3C */
    if (pid == 0 || pid > 0x40) {
        /* 0x00E14E42 */
        *status_ret = status_$illegal_process_id;
        return;
    }

    /* 0x00E14E4C..0x00E14E56 */
    pcb = PCBS[pid];

    /* 0x00E14E5A: btst.b #0x3,(0x55,A3) */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        /* 0x00E14E62 */
        *status_ret = status_$process_not_bound;
        return;
    }

    /* 0x00E14E6C */
    if (pcb == PROC1_$CURRENT_PCB) {
        /* 0x00E14E74 */
        PMAP_$PURGE_WS((int16_t)pid, 0);

        /* 0x00E14E80: ori #0x700,SR */
        SET_IPL7();

        /* 0x00E14E84 */
        PROC1_$TRY_TO_SUSPEND(pcb);

        /* 0x00E14E8C: btst.b #0x1,(0x55,A3) */
        if ((pcb->pri_max & PROC1_FLAG_SUSPENDED) == 0) {
            /* 0x00E14E94 */
            CRASH_SYSTEM(&proc1_$not_suspendable_00e14f4e);
        }
    } else {
        /* 0x00E14EA2: btst.b #0x1,(0x55,A3) */
        if ((pcb->pri_max & PROC1_FLAG_SUSPENDED) == 0) {
            /* 0x00E14EAA */
            ec_value = PROC1_$SUSPEND_EC.value;

            /* 0x00E14EB0 */
            suspended = PROC1_$SUSPEND(pid, status_ret);

            /* 0x00E14EC2 / 0x00E14EC6 */
            wait_value = ec_value + 1;

            /* 0x00E14EF0: tst.b D0b / bpl 0x00E14ECA */
            while (suspended >= 0) {
                /* 0x00E14ECA..0x00E14EE0 */
                EC_$WAIT((ec_$wait_ecs_t){ { &PROC1_$SUSPEND_EC, NULL, NULL } },
                         (ec_$wait_vals_t){ { wait_value, 0, 0 } });

                /* 0x00E14EE4 */
                suspended = PROC1_$SUSPENDP(pid, status_ret);
            }
        }

        /* 0x00E14EF4 */
        PMAP_$PURGE_WS((int16_t)pid, 0);

        /* 0x00E14F00: ori #0x700,SR */
        SET_IPL7();
    }

    /* 0x00E14F04..0x00E14F18 */
    TIME_$Q_FLUSH_QUEUE(&TIME_$VTQ[pid - 1]);

    /* 0x00E14F20: bclr.b #0x3,(0x55,A3) */
    pcb->pri_max = (uint8_t)(pcb->pri_max & ~PROC1_FLAG_BOUND);

    /* 0x00E14F26..0x00E14F2E */
    PROC1_$FREE_STACK(OS_STACK_BASE[pid]);

    /* 0x00E14F34 */
    PROC1_$SET_TYPE(pid, 0);

    /* 0x00E14F3E: PROC1_$DISPATCH lowers the IPL */
    PROC1_$DISPATCH();
}
