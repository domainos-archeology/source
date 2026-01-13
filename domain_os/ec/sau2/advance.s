/*
 * EC_$ADVANCE - Advance eventcount and dispatch
 *
 * Increments eventcount, wakes eligible waiters, and calls dispatcher.
 * Uses interrupt disable for synchronization (IPL 7).
 *
 * Parameters:
 *   4(SP) - Pointer to eventcount structure
 *
 * Original address: 0x00e206ee
 */

    .text
    .globl  EC_$ADVANCE
    .globl  _EC_$ADVANCE

EC_$ADVANCE:
_EC_$ADVANCE:
    ori.w   #0x0700, %sr            /* Disable interrupts (IPL = 7) */
    movea.l 4(%sp), %a0             /* Load eventcount pointer */
    bsr.w   ADVANCE_INT             /* Call internal advance */
    bsr.w   PROC1_$DISPATCH_INT     /* Call dispatcher */
    andi.w  #0xF8FF, %sr            /* Restore interrupts (clear IPL) */
    rts
