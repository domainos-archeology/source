/*
 * EC_$ADVANCE_WITHOUT_DISPATCH - Advance without calling dispatcher
 *
 * Like EC_$ADVANCE but does not call PROC1_$DISPATCH_INT.
 * Saves and restores SR for synchronization.
 *
 * Parameters:
 *   4(SP) - Pointer to eventcount structure
 *
 * Original address: 0x00e20718
 */

    .text
    .globl  EC_$ADVANCE_WITHOUT_DISPATCH
    .globl  _EC_$ADVANCE_WITHOUT_DISPATCH

EC_$ADVANCE_WITHOUT_DISPATCH:
_EC_$ADVANCE_WITHOUT_DISPATCH:
    move.w  %sr, -(%sp)             /* Save current SR */
    ori.w   #0x0700, %sr            /* Disable interrupts (IPL = 7) */
    movea.l 6(%sp), %a0             /* Load eventcount pointer (offset by saved SR) */
    bsr.w   ADVANCE_INT             /* Call internal advance */
    move.w  (%sp)+, %sr             /* Restore saved SR */
    rts
