/*
 * EC_$ADVANCE_ALL - Wake all waiters on eventcount
 *
 * Sets value to 0x7FFFFFFF to wake all waiters.
 * Uses interrupt disable for synchronization.
 *
 * Parameters:
 *   4(SP) - Pointer to eventcount structure
 *
 * Original address: 0x00e20702
 */

    .text
    .globl  EC_$ADVANCE_ALL
    .globl  _EC_$ADVANCE_ALL

EC_$ADVANCE_ALL:
_EC_$ADVANCE_ALL:
    ori.w   #0x0700, %sr            /* Disable interrupts (IPL = 7) */
    movea.l 4(%sp), %a0             /* Load eventcount pointer */
    bsr.w   ADVANCE_ALL_INT         /* Call internal advance all */
    bsr.w   PROC1_$DISPATCH_INT     /* Call dispatcher */
    andi.w  #0xF8FF, %sr            /* Restore interrupts (clear IPL) */
    rts
