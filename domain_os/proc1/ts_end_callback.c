/*
 * PROC1_$TS_END_CALLBACK - Timeslice end callback
 * Original address: 0x00e14a70
 *
 * Called when a process's timeslice expires. Decrements the process
 * priority/state and requeues it in the ready list if needed.
 *
 * Parameters:
 *   timer_info - Pointer to timer callback info structure
 */

#include "proc1.h"

/* Timeslice values table indexed by state - 0xe205d2 */
#if defined(M68K)
    #define TIMESLICE_TABLE     ((int16_t*)0xe205d2)
#else
    extern int16_t TIMESLICE_TABLE[];
#endif

/*
 * Timer callback info structure
 * The first field points to another structure containing PID at offset 0x0a
 */
typedef struct ts_callback_info_t {
    struct ts_callback_block_t *block;  /* Pointer to callback block */
} ts_callback_info_t;

typedef struct ts_callback_block_t {
    uint32_t field_00;      /* 0x00 */
    uint32_t field_04;      /* 0x04 */
    uint16_t field_08;      /* 0x08 */
    uint16_t pid;           /* 0x0a: Process ID */
} ts_callback_block_t;

void PROC1_$TS_END_CALLBACK(ts_callback_info_t *timer_info)
{
    proc1_t *pcb;
    uint16_t pid;
    int16_t new_state;
    int16_t min_state;
    int16_t new_timeslice;
    uint16_t saved_sr;

    /* Get PID from callback info */
    pid = timer_info->block->pid;

    /* Get PCB for this process */
    pcb = PCBS[pid];

    DISABLE_INTERRUPTS(saved_sr);

    /* Special handling for PID 2 (idle process?) - just reschedule */
    if (pid == 2) {
        new_timeslice = -1;  /* Max timeslice for idle */
    } else {
        /* Decrement state (lower number = higher priority) */
        pcb->state--;

        /* Get minimum allowed state from inh_count field */
        min_state = pcb->inh_count;

        /* Clamp state to minimum */
        if (pcb->state < min_state) {
            pcb->state = min_state;
        }

        /* Reorder in ready list based on whether locks are held */
        if (pcb->resource_locks_held == 0) {
            /* No locks - remove and re-add to get correct position */
            PROC1_$REMOVE_READY(pcb);
            PROC1_$ADD_READY(pcb);
        } else {
            /* Has locks - just reorder and set deferred flag */
            PROC1_$REORDER_READY();
            pcb->pri_max |= 0x10;  /* Set deferred reorder flag */
        }

        /* Get new timeslice based on current state */
        new_timeslice = TIMESLICE_TABLE[pcb->state - 1];
    }

    ENABLE_INTERRUPTS(saved_sr);

    /* Reschedule the timeslice timer */
    PROC1_$SET_TS(pcb, new_timeslice);
}
