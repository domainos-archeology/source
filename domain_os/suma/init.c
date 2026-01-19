/*
 * SUMA_$INIT - Initialize the SUMA (tablet pad) subsystem
 *
 * Initializes the tablet state structure:
 *   - Sets tpad_buffer pointer to TERM_$TPAD_BUFFER
 *   - Clears receive state to 0
 *   - Sets initial threshold to 0x200
 *
 * From: 0x00e33224
 *
 * Original assembly:
 *   00e33224    link.w A6,0x0
 *   00e33228    movea.l #0xe2dd88,A0        ; SUMA_$STATE base
 *   00e3322e    move.l #0xe2de3c,(0x24,A0)  ; tpad_buffer = &TERM_$TPAD_BUFFER
 *   00e33236    clr.w (0x28,A0)             ; rcv_state = 0
 *   00e3323a    move.b #0x1,(0x1e,A0)       ; cur_id_flags = 1 (initial)
 *   00e33240    move.w #0x200,(0x2a,A0)     ; threshold = 0x200
 *   00e33246    unlk A6
 *   00e33248    rts
 */

#include "suma/suma_internal.h"

void SUMA_$INIT(void)
{
    /* Set tpad_buffer pointer to TERM_$TPAD_BUFFER */
    SUMA_$STATE.tpad_buffer = &TERM_$TPAD_BUFFER;

    /* Clear receive state machine */
    SUMA_$STATE.rcv_state = 0;

    /* Set initial ID flags (seems unused but set in original) */
    SUMA_$STATE.cur_id_flags = 1;

    /* Set initial position threshold */
    SUMA_$STATE.threshold = SUMA_INITIAL_THRESHOLD;
}
