/*
 * PROC1_$UNBIND - Unbind a process from its PCB
 * Original address: 0x00e14e24
 *
 * Releases a process's resources and frees its PCB slot.
 * Handles both current process (self-termination) and other processes.
 *
 * Parameters:
 *   pid - Process ID to unbind
 *   status_ret - Status return pointer
 */

#include "proc1/proc1_internal.h"
#include "pmap/pmap.h"
#include "time/time.h"
#include "misc/misc.h"

#define TS_QUEUE_ELEM_SIZE  12

/* Error message for failed self-suspend */
static const char UNBIND_CRASH_MSG[] = "PROC1_$UNBIND: self-suspend failed";

void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret)
{
    proc1_t *pcb;
    int8_t suspend_result;
    int32_t suspend_ec_val;
    int32_t wait_val;
    uint16_t saved_sr;
    char *queue_elem;

    /* Validate PID */
    if (pid == 0 || pid > 0x40) {
        *status_ret = status_$illegal_process_id;
        return;
    }

    /* Get PCB */
    pcb = PCBS[pid];

    /* Check if bound */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        *status_ret = status_$process_not_bound;
        return;
    }

    if (pcb == PROC1_$CURRENT_PCB) {
        /*
         * Self-termination case
         * Purge working set and suspend ourselves
         */
        PMAP_$PURGE_WS(pid, 0);

        DISABLE_INTERRUPTS(saved_sr);
        PROC1_$TRY_TO_SUSPEND(pcb);

        /* Verify we are now suspended */
        if ((pcb->pri_max & PROC1_FLAG_SUSPENDED) == 0) {
            /* Should not happen - crash the system */
            CRASH_SYSTEM((void*)UNBIND_CRASH_MSG);
        }
    } else {
        /*
         * Terminating another process
         * Need to wait for it to become suspended
         */
        if ((pcb->pri_max & PROC1_FLAG_SUSPENDED) == 0) {
            /* 0x00E14EAA: read PROC1_$SUSPEND_EC.value (0xE205F6) */
            suspend_ec_val = PROC1_$SUSPEND_EC.value;

            /* 0x00E14EB6: try to suspend */
            suspend_result = PROC1_$SUSPEND(pid, status_ret);

            /* 0x00E14EC6: D5 = D4 + 1, computed once outside the loop */
            wait_val = suspend_ec_val + 1;

            /* 0x00E14EF0: tst.b D0b / bpl -- loop while the boolean is
             * false (a Domain boolean is 0xFF when true) */
            while (suspend_result >= 0) {
                /*
                 * 0x00E14ECA-0x00E14EE0: EC_$WAIT takes two 3-element
                 * arrays by value; the caller pops all 24 bytes.
                 *   ecs  = { &PROC1_$SUSPEND_EC, NULL, NULL }
                 *   vals = { suspend_ec_val + 1, 0, 0 }
                 */
                EC_$WAIT((ec_$wait_ecs_t){ { &PROC1_$SUSPEND_EC, NULL, NULL } },
                         (ec_$wait_vals_t){ { wait_val, 0, 0 } });

                /* 0x00E14EEA: check if now suspended */
                suspend_result = PROC1_$SUSPENDP(pid, status_ret);
            }
        }

        /* Purge working set */
        PMAP_$PURGE_WS(pid, 0);

        DISABLE_INTERRUPTS(saved_sr);
    }

    /* Flush the timer queue for this process */
    queue_elem = &TS_QUEUE_TABLE[pid * TS_QUEUE_ELEM_SIZE] - 12;
    TIME_$Q_FLUSH_QUEUE((time_queue_t *)queue_elem);

    /* Clear bound flag */
    pcb->pri_max &= ~PROC1_FLAG_BOUND;

    /* Free the process stack */
    PROC1_$FREE_STACK(OS_STACK_BASE[pid]);

    /* Clear process type */
    PROC1_$SET_TYPE(pid, 0);

    /* Dispatch to another process */
    PROC1_$DISPATCH();
}
