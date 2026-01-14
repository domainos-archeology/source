/*
 * PROC1_$INIT - Initialize process management subsystem
 * Original address: 0x00e2f958
 *
 * Initializes the process management data structures and starts
 * the initial processes. Sets up:
 * - Stack allocation memory regions
 * - Initial process (PID 2) in the ready list
 * - Timeslice timers for PIDs 1 and 2
 * - Working set scanning for PIDs 1 and 2
 */

#include "proc1.h"

void PROC1_$INIT(void)
{
    proc1_t *pcb;
    uint16_t saved_sr;

    /*
     * Initialize stack allocation regions
     * Low region at 0xD00000 grows upward
     * High region at 0xD50000 grows downward
     * These values are from proc1_config.h
     */
    STACK_LOW_WATER = (void *)PROC1_STACK_LOW_START;
    STACK_HIGH_WATER = (void *)PROC1_STACK_HIGH_START;
    STACK_FREE_LIST = NULL;  /* Empty free list */

    /* Set OS stack for PID 1 */
    OS_STACK_BASE[1] = (void *)PROC1_OS_STACK_BASE;

    /* Set process type for PID 2 to type 3 (kernel daemon?) */
    PROC1_$SET_TYPE(2, 3);

    /* Get PCB for initial process (PID 2) */
    pcb = PCBS[2];

    /* Initialize PCB fields:
     * state = 0x10 (priority 16)
     * inh_count = 0x0001, sw_bsr = 0x0010
     */
    pcb->state = 0x10;
    pcb->inh_count = 0x0001;
    pcb->sw_bsr = 0x0010;
    pcb->resource_locks_held = 0;

    DISABLE_INTERRUPTS(saved_sr);

    /* Add to ready list or reorder if already there */
    if ((pcb->pri_max & 0x0b) == PROC1_FLAG_BOUND) {
        /* Already bound and not waiting/suspended - reorder */
        PROC1_$REORDER_READY();
    } else {
        /* Set bound flag and add to ready list */
        pcb->pri_max = PROC1_FLAG_BOUND;
        PROC1_$ADD_READY(pcb);
    }

    /* Make PID 2 the current process */
    PROC1_$CURRENT_PCB = PCBS[2];
    PROC1_$CURRENT = PCBS[2]->mypid;

    /* Initialize timeslice timers for PIDs 2 and 1 */
    PROC1_$INIT_TS_TIMER(2);
    PROC1_$INIT_TS_TIMER(1);

    /* Dispatch - this starts the scheduler */
    PROC1_$DISPATCH();

    /* Initialize working set scanning:
     * PID 2 with param 5, PID 1 with param 7
     */
    PMAP_$INIT_WS_SCAN(2, 5);
    PMAP_$INIT_WS_SCAN(1, 7);
}
