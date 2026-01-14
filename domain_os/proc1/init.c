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

/* Stack allocation globals (relative to base 0xe254e8) */
#if defined(M68K)
    /* Stack region low water mark (grows up) - base + 0xc40 */
    #define STACK_LOW_WATER     (*(uint32_t*)0xe26128)
    /* Stack region high water mark (grows down) - base + 0xc3c */
    #define STACK_HIGH_WATER    (*(uint32_t*)0xe26124)
    /* Free list of 4KB stacks - base + 0xc38 */
    #define STACK_FREE_LIST     (*(uint32_t*)0xe26120)
    /* OS stack for PID 1 - base + 0x734 = 0xe25c1c */
    #define OS_STACK_PID1       (*(uint32_t*)0xe25c1c)
    /* PCB pointer for PID 2 (idle?) */
    #define INIT_PCB            (*(proc1_t**)0xe1ead0)
#else
    extern uint32_t STACK_LOW_WATER;
    extern uint32_t STACK_HIGH_WATER;
    extern uint32_t STACK_FREE_LIST;
    extern uint32_t OS_STACK_PID1;
    extern proc1_t *INIT_PCB;
#endif

/* Memory region constants */
#define STACK_LOW_START     0x00D00000  /* Start of low stack region */
#define STACK_HIGH_START    0x00D50000  /* Start of high stack region */
#define OS_STACK_BASE       0x00EB2000  /* OS stack base */

/* External functions */
extern void PROC1_$SET_TYPE(uint16_t pid, uint16_t type);
extern void PROC1_$REORDER_READY(void);
extern void PROC1_$ADD_READY(proc1_t *pcb);
extern void PROC1_$INIT_TS_TIMER(uint16_t pid);
extern void PROC1_$DISPATCH(void);
extern void PMAP_$INIT_WS_SCAN(uint16_t pid, uint16_t param);

void PROC1_$INIT(void)
{
    proc1_t *pcb;
    uint16_t saved_sr;

    /*
     * Initialize stack allocation regions
     * Low region at 0xD00000 grows upward
     * High region at 0xD50000 grows downward
     */
    STACK_LOW_WATER = STACK_LOW_START;
    STACK_HIGH_WATER = STACK_HIGH_START;
    STACK_FREE_LIST = 0;  /* Empty free list */

    /* Set OS stack for PID 1 */
    OS_STACK_PID1 = OS_STACK_BASE;

    /* Set process type for PID 2 to type 3 (kernel daemon?) */
    PROC1_$SET_TYPE(2, 3);

    /* Get PCB for initial process (PID 2) */
    pcb = INIT_PCB;

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
    PROC1_$CURRENT_PCB = INIT_PCB;
    PROC1_$CURRENT = INIT_PCB->mypid;

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
