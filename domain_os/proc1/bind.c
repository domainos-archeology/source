/*
 * PROC1_$BIND - Bind a process to a PCB
 * Original address: 0x00e14d1c
 *
 * Allocates a PCB slot for a new process and initializes it.
 * Scans the PCB table (starting at slot 3) for an unbound slot.
 *
 * Parameters:
 *   proc_startup - Process startup function/entry point
 *   stack1 - Stack parameter 1 (unused in this version?)
 *   stack - Stack allocation to use
 *   ws_param - Working set parameter for PMAP_$INIT_WS_SCAN
 *   status_p - Status return pointer
 *
 * Returns:
 *   Process ID (PID) on success
 */

#include "proc1/proc1_internal.h"
#include "ml/ml.h"
#include "pmap/pmap.h"

/*
 * Initial CPU time value (8 bytes at 0xe14e1c)
 * This appears to be zeros - initial CPU time
 */
static const uint8_t INIT_CPU_TIME[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

uint16_t PROC1_$BIND(void *proc_startup, void *stack1, void *stack,
                      uint16_t ws_param, status_$t *status_p)
{
    uint16_t pid;
    proc1_t *pcb;
    uint32_t *stats;
    int i;

    /* Acquire process creation lock */
    ML_$LOCK(PROC1_CREATE_LOCK_ID);

    /* Search for free PCB starting at PID 3 (0, 1, 2 are reserved) */
    for (pid = 3; pid <= 0x40; pid++) {
        pcb = PCBS[pid];

        /* Check if slot is not bound (bit 3 at offset 0x55) */
        if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
            /* Found free slot */
            *status_p = status_$ok;

            /* Store stack in OS stack table */
            OS_STACK_BASE[pid] = stack;

            /* Initialize working set scanning */
            PMAP_$INIT_WS_SCAN(pid, ws_param);

            /* Clear resource locks */
            pcb->resource_locks_held = 0;

            /* Initialize inh_count and sw_bsr to 0x0001 and 0x0001 */
            /* Assembly shows: move.l #0x10010,(0x56,A2) which sets:
             * offset 0x56 (inh_count) = 0x0001
             * offset 0x58 (sw_bsr) = 0x0001
             * But actually it's storing at 0x56 as a long, so:
             * 0x56-0x57 = 0x0001 (inh_count)
             * 0x58-0x59 = 0x0010 = 16? Let me check the byte order...
             * 0x00010010 big-endian: byte[0]=0, byte[1]=1, byte[2]=0, byte[3]=0x10
             * So inh_count = 0x0001, sw_bsr = 0x0010 = 16
             */
            pcb->inh_count = 0x0001;
            pcb->sw_bsr = 0x0010;

            /* Initialize CPU time from template (copy 8 bytes) */
            for (i = 0; i < 8; i++) {
                ((uint8_t*)&pcb->cpu_total)[i] = INIT_CPU_TIME[i];
            }

            /* Clear ASID */
            pcb->asid = 0;

            /* Clear process statistics (16 bytes per process) */
            stats = &PROC_STATS_BASE[pid * 4];
            stats[0] = 0;
            stats[1] = 0;
            stats[2] = 0;
            stats[3] = 0;

            /* Set initial state (pri_min:pri_max at 0x54-0x55)
             * 0x000a = state 10, with bound flag will be set later */
            pcb->pri_min = 0;
            pcb->pri_max = 0x0a;  /* Will OR with BOUND flag later */

            /* Clear fields at 0x60 and 0x64 */
            pcb->field_60 = 0;
            pcb->field_64 = 0;

            /* Release lock */
            ML_$UNLOCK(PROC1_CREATE_LOCK_ID);

            /* Initialize stack with startup parameters */
            INIT_STACK(pcb, &proc_startup);

            /* Initialize timeslice timer */
            PROC1_$INIT_TS_TIMER(pid);

            return pid;
        }
    }

    /* No free PCB available */
    *status_p = status_$no_pcb_is_available;
    ML_$UNLOCK(PROC1_CREATE_LOCK_ID);

    return 0;  /* Invalid PID indicates failure */
}
