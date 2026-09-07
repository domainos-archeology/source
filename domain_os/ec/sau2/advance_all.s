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

/*
 * Section note: gas fixes the pre-created .text section's alignment at 2**2
 * and offers no directive to lower it, which pads this object out to a
 * longword boundary and shifts the rest of the run off the image's gaps.  A
 * section created with `.section' starts at 2**0 and `.balign 2' raises it to
 * exactly the m68k requirement, so the four ec/sau2 objects link contiguously.
 * sau2.ld names these sections explicitly (source-0ke7).
 */
        .section .text.ec_advance_all,"ax",@progbits
        .balign 2

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
