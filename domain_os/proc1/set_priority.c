/*
 * PROC1_$SET_PRIORITY - Set or get process priority range
 * Original address: 0x00e1523c
 *
 * Sets the minimum and maximum priority values for a process, or
 * retrieves the current values.
 *
 * Parameters:
 *   pid - Process ID
 *   mode - Operation mode:
 *          mode < 0: Set priorities from min_priority and max_priority
 *          mode >= 0: Get current priorities into min_priority and max_priority
 *   min_priority - Pointer to minimum priority value
 *   max_priority - Pointer to maximum priority value
 *
 * Priority values are clamped to range [1, 16].
 */

#include "proc1.h"

/* External crash function */
extern void CRASH_SYSTEM(const char *msg);

/* Error message */
static const char ILLEGAL_PID_MSG[] = "Illegal process id";

/*
 * clamp_priority - Clamp priority value to valid range [1, 16]
 * This is an inline version of FUN_00e15222
 */
static uint16_t clamp_priority(uint16_t value)
{
    if (value < 2) {
        return 1;
    }
    if (value > 15) {
        return 16;
    }
    return value;
}

void PROC1_$SET_PRIORITY(uint16_t pid, int16_t mode, uint16_t *min_priority, uint16_t *max_priority)
{
    proc1_t *pcb;
    uint16_t new_min, new_max;
    uint16_t saved_sr;

    /* Validate PID */
    if (pid == 0 || pid > 0x40) {
        CRASH_SYSTEM(ILLEGAL_PID_MSG);
        return;
    }

    /* Get PCB */
    pcb = PCBS[pid];

    if (mode < 0) {
        /* Set mode: apply new priority values */
        new_min = clamp_priority(*min_priority);
        pcb->inh_count = new_min;  /* Min priority stored at offset 0x56 */

        new_max = clamp_priority(*max_priority);
        pcb->sw_bsr = new_max;     /* Max priority stored at offset 0x58 */

        DISABLE_INTERRUPTS(saved_sr);

        /*
         * Adjust current state if outside new priority range:
         * - If current state > max, set to max
         * - If current state < min, set to min
         */
        if (new_max < pcb->state) {
            pcb->state = new_max;
        } else if (pcb->state < new_min) {
            pcb->state = new_min;
        }

        /*
         * If process is runnable (bound but not waiting or suspended),
         * reorder in ready list and dispatch
         * Check: (flags & 0x0b) == 0x08 means bound but not waiting/suspended
         */
        if ((pcb->pri_max & 0x0b) == PROC1_FLAG_BOUND) {
            PROC1_$REORDER_READY();
            PROC1_$DISPATCH();
        }

        ENABLE_INTERRUPTS(saved_sr);
    } else {
        /* Get mode: return current priority values */
        *min_priority = pcb->inh_count;
        *max_priority = pcb->sw_bsr;
    }
}
