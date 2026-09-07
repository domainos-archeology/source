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
    /*
     * Image: 0xE206F6  61 34  bsr.b ADVANCE_INT  (0xE206F8 + 0x34 = 0xE2072C)
     *
     * Emitted as the literal image encoding, not as `bsr.b ADVANCE_INT'.
     * gas does keep the two-byte form for the symbolic operand (it emits
     * 61 00 plus an R_68K_PC8 fixup on the displacement byte), but the link
     * cannot resolve it: ADVANCE_INT is the C routine ec/advance_int.c, and
     * sau2.ld's ec-object gather takes objects in Makefile order, so every
     * ec C object precedes the ec/sau2 assembly objects and ADVANCE_INT
     * lands ~0xDD0 bytes ahead of this branch - far outside the +/-127 byte
     * reach of a byte displacement ("relocation truncated to fit:
     * R_68K_PC8").  `bsr.w' relocates cleanly but is 4 bytes and displaces
     * every following instruction, so byte fidelity wins here.
     *
     * TODO(source-mc3k, 0xE206F6): restore `bsr.b ADVANCE_INT' once sau2.ld
     * places ec/advance_int.o after the ec/sau2 objects, in image order.
     */
    .short  0x6134                  /* bsr.b ADVANCE_INT */
    bsr.w   PROC1_$DISPATCH_INT     /* Call dispatcher */
    andi.w  #0xF8FF, %sr            /* Restore interrupts (clear IPL) */
    rts
