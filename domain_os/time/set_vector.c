/*
 * TIME_$SET_VECTOR - Set timer interrupt vector
 *
 * Sets the interrupt vector for the timer hardware.
 * On M68K, this writes to the vector table at address 0x78.
 *
 * Original address: 0x00e2b102
 *
 * Assembly:
 *   00e2b102    lea (0x2c,PC),A0          ; Address of interrupt handler
 *   00e2b106    move.l A0,(0x00000078).l  ; Store in vector 0x78
 *   00e2b10c    rts
 *
 * Vector 0x78 = interrupt level 6, autovector
 */

#include "time/time_internal.h"

/* Interrupt vector table address for timer */
#define TIME_VECTOR_ADDRESS 0x00000078

void TIME_$SET_VECTOR(void)
{
    volatile uint32_t *vector = (volatile uint32_t *)TIME_VECTOR_ADDRESS;
    *vector = (uint32_t)(uintptr_t)TIME_$TIMER_HANDLER;
}
