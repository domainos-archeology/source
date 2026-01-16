/*
 * PROC1_$SET_VT - Set virtual timer for a process
 * Original address: 0x00e1495c
 *
 * Sets the virtual timer value for a process. If the process is the
 * current process, it updates the hardware timer immediately.
 *
 * Parameters:
 *   pid - Process ID
 *   time_value - Pointer to 6-byte time value (4-byte high + 2-byte low)
 *                If high word is non-zero, sets timer to max (-1)
 *   status_ret - Status return pointer
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"
#include "cal/cal.h"

void PROC1_$SET_VT(uint16_t pid, uint32_t *time_value, status_$t *status_ret)
{
    proc1_t *pcb;
    int16_t new_vtimer;
    int16_t old_vtimer;
    uint32_t delta_high;
    int16_t delta_low;
    uint16_t saved_sr;

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

    /* Determine new vtimer value:
     * If high word of time is non-zero, use -1 (max)
     * Otherwise use low word
     */
    if (time_value[0] != 0) {
        new_vtimer = -1;
    } else {
        /* time_value[1] is the low 2 bytes at offset 4 */
        new_vtimer = (int16_t)((uint16_t *)time_value)[2];
    }

    /* If this is the current process, need special handling */
    if (pcb == PROC1_$CURRENT_PCB) {
        DISABLE_INTERRUPTS(saved_sr);

        /* Read current timer value */
        delta_high = 0;
        delta_low = pcb->vtimer - TIME_$VT_TIMER();

        /* Accumulate time into CPU total */
        ADD48((clock_t *)&pcb->cpu_total, (clock_t *)&delta_high);

        /* Set new vtimer value */
        pcb->vtimer = new_vtimer;

        /* Write to hardware virtual timer */
        TIME_$WRT_VT_TIMER(pcb->vtimer);

        ENABLE_INTERRUPTS(saved_sr);
    } else {
        /* For non-current process, just set the value */
        pcb->vtimer = new_vtimer;
    }

    *status_ret = status_$ok;
}
