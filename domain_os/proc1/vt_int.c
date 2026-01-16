/*
 * PROC1_$VT_INT - Virtual timer interrupt handler
 * Original address: 0x00e1491e
 *
 * Called on virtual timer interrupts. Accumulates CPU time for the
 * current process and returns the accumulated CPU time.
 *
 * Parameters:
 *   cpu_time_out - Pointer to receive CPU time (6 bytes: 4-byte high + 2-byte low)
 */

#include "proc1/proc1_internal.h"
#include "cal/cal.h"

/*
 * CPU time structure (6 bytes total)
 * This matches the Apollo's 48-bit time format.
 */
typedef struct {
    uint32_t high;   /* High 32 bits */
    uint16_t low;    /* Low 16 bits */
} cpu_time_t;

void PROC1_$VT_INT(void *cpu_time_arg)
{
    cpu_time_t *cpu_time_out = (cpu_time_t *)cpu_time_arg;
    proc1_t *pcb;
    cpu_time_t delta;

    pcb = PROC1_$CURRENT_PCB;

    /* Build delta from vtimer value */
    delta.high = 0;
    delta.low = pcb->vtimer;

    /* Add delta to accumulated CPU time (48-bit addition) */
    ADD48((clock_t *)&pcb->cpu_total, (clock_t *)&delta);

    /* Reset virtual timer */
    pcb->vtimer = 0;

    /* Return accumulated CPU time */
    cpu_time_out->high = pcb->cpu_total;
    cpu_time_out->low = pcb->cpu_usage;
}
