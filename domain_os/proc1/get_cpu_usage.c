/*
 * PROC1_$GET_CPU_USAGE - Get CPU usage for current process
 * Original address: 0x00e208aa
 *
 * Returns CPU time (shifted) plus two additional statistics
 * from the current process's PCB.
 *
 * Parameters:
 *   time_ret - Pointer to receive CPU time (6 bytes, shifted left by 1)
 *   stat1_ret - Pointer to receive field_60 from PCB
 *   stat2_ret - Pointer to receive field_64 from PCB
 */

#include "proc1.h"

/* External TIME_$VT_TIMER */
extern int16_t TIME_$VT_TIMER(void);

void PROC1_$GET_CPU_USAGE(void *time_ret, uint32_t *stat1_ret, uint32_t *stat2_ret)
{
    proc1_t *pcb;
    int16_t vt_current;
    uint32_t high;
    uint16_t low;
    uint16_t delta;
    uint16_t saved_sr;
    uint32_t *out_high = (uint32_t*)time_ret;
    uint16_t *out_low = (uint16_t*)((char*)time_ret + 4);

    DISABLE_INTERRUPTS(saved_sr);

    /* Get current virtual timer value */
    vt_current = TIME_$VT_TIMER();

    pcb = PROC1_$CURRENT_PCB;

    /* Calculate delta and total time */
    delta = pcb->vtimer - vt_current;
    high = pcb->cpu_total;
    low = pcb->cpu_usage + delta;
    if (low < delta) {
        high++;
    }

    ENABLE_INTERRUPTS(saved_sr);

    /* Shift left by 1 (48-bit) */
    low <<= 1;
    high = (high << 1) | (low >> 15);

    *out_high = high;
    *out_low = low & 0xFFFF;

    /* Return additional statistics */
    *stat1_ret = pcb->field_60;
    *stat2_ret = pcb->field_64;
}
