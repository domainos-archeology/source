/*
 * TIME_$GET_ADJUST - Get clock adjustment values
 *
 * Returns the current clock adjustment delta in seconds and microseconds.
 * Uses 64-bit math to convert from internal tick representation.
 *
 * Parameters:
 *   delta - Pointer to receive delta as timeval (seconds, microseconds)
 *
 * Original address: 0x00e16aa8
 *
 * Assembly:
 *   00e16aa8    link.w A6,-0x4
 *   00e16aac    movea.l (0x8,A6),A1       ; delta pointer
 *   00e16ab0    ori #0x700,SR             ; Disable interrupts
 *   00e16ab4    move.l TIME_$CURRENT_DELTA,D1
 *   00e16aba    andi #-0x701,SR           ; Enable interrupts
 *   00e16abe    move.l #0x3d090,-(SP)     ; 250000
 *   00e16ac4    move.l D1,-(SP)
 *   00e16ac6    jsr M_DIS_LLL             ; delta / 250000 = seconds
 *   00e16acc    addq.w #0x8,SP
 *   00e16ace    move.l D0,(A1)            ; Store seconds
 *   00e16ad0    move.l #0x3d090,-(SP)     ; 250000
 *   00e16ad6    move.l D1,-(SP)
 *   00e16ad8    jsr M_OIS_LLL             ; delta % 250000 = ticks
 *   00e16ade    lsl.l #0x2,D0             ; ticks * 4 = microseconds
 *   00e16ae0    move.l D0,(0x4,A1)        ; Store microseconds
 *
 * Constants:
 *   250000 = 0x3D090 = ticks per second
 *   Each tick = 4 microseconds
 */

#include "time.h"
#include "arch/m68k/arch.h"

/* Ticks per second */
#define TICKS_PER_SECOND 250000

void TIME_$GET_ADJUST(int32_t *delta)
{
    uint16_t saved_sr;
    int32_t current_delta;

    /* Read delta with interrupts disabled for consistency */
    GET_SR(saved_sr);
    SET_SR(saved_sr | SR_IPL_DISABLE_ALL);
    current_delta = (int32_t)TIME_$CURRENT_DELTA;
    SET_SR(saved_sr);

    /* Convert to seconds and microseconds */
    delta[0] = current_delta / TICKS_PER_SECOND;
    delta[1] = (current_delta % TICKS_PER_SECOND) * 4;  /* 4 usec per tick */
}
