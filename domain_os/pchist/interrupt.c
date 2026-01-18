/*
 * PCHIST_$INTERRUPT - Record PC sample from interrupt
 *
 * Reverse engineered from Domain/OS at address 0x00e1a1f6
 */

#include "pchist/pchist_internal.h"

/*
 * Constant mode value used for interrupt-level sampling
 * The value 0x0000 is pushed as the mode parameter
 * (PC-relative reference to data after the RTS instruction)
 */
static int16_t interrupt_mode = 0;

/*
 * PCHIST_$INTERRUPT
 *
 * Simple wrapper around PCHIST_$COUNT for interrupt-level
 * PC sampling. Uses a fixed mode value of 0.
 */
void PCHIST_$INTERRUPT(uint32_t *pc_ptr)
{
    PCHIST_$COUNT(pc_ptr, &interrupt_mode);
}
