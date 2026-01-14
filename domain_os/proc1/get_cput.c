/*
 * PROC1_$GET_CPUT - Get CPU time for current process
 * Original address: 0x00e20894
 *
 * Returns the accumulated CPU time for the current process.
 * The time is returned as a 48-bit value but shifted left by 1 bit.
 *
 * Parameters:
 *   time_ret - Pointer to receive CPU time (6 bytes)
 *
 * Note: PROC1_$GET_CPUT8 (at 0x00e2089c) returns the unshifted value.
 */

#include "proc1.h"

/*
 * get_current_cpu_time - Internal helper to compute current CPU time
 * Original: FUN_00e208d0
 *
 * Returns:
 *   D0:D1 = CPU time (D0=low 16 bits, D1=high 32 bits)
 *
 * The computation is:
 *   cpu_time = pcb->cpu_total + pcb->cpu_usage + (pcb->vtimer - TIME_$VT_TIMER())
 */
static void get_current_cpu_time(uint32_t *high_out, uint16_t *low_out)
{
    proc1_t *pcb;
    int16_t vt_current;
    uint16_t delta;
    uint32_t high;
    uint16_t low;
    uint16_t saved_sr;

    DISABLE_INTERRUPTS(saved_sr);

    /* Get current virtual timer value */
    vt_current = TIME_$VT_TIMER();

    pcb = PROC1_$CURRENT_PCB;

    /* Calculate delta: vtimer - current_timer_value */
    delta = pcb->vtimer - vt_current;

    /* Get base accumulated time */
    high = pcb->cpu_total;
    low = pcb->cpu_usage;

    /* Add delta with carry */
    low += delta;
    if (low < delta) {
        high++;  /* Carry */
    }

    ENABLE_INTERRUPTS(saved_sr);

    *high_out = high;
    *low_out = low;
}

/*
 * PROC1_$GET_CPUT - Get CPU time (shifted by 1)
 */
void PROC1_$GET_CPUT(void *time_ret)
{
    uint32_t high;
    uint16_t low;
    uint32_t *out_high = (uint32_t*)time_ret;
    uint16_t *out_low = (uint16_t*)((char*)time_ret + 4);

    get_current_cpu_time(&high, &low);

    /* Shift left by 1 bit (48-bit shift) */
    low <<= 1;
    high = (high << 1) | (low >> 15);  /* Carry from low */

    *out_high = high;
    *out_low = low & 0xFFFF;
}

/*
 * PROC1_$GET_CPUT8 - Get CPU time (unshifted)
 */
void PROC1_$GET_CPUT8(void *time_ret)
{
    uint32_t high;
    uint16_t low;
    uint32_t *out_high = (uint32_t*)time_ret;
    uint16_t *out_low = (uint16_t*)((char*)time_ret + 4);

    get_current_cpu_time(&high, &low);

    *out_high = high;
    *out_low = low;
}
