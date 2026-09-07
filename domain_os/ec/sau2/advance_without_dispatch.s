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
    /*
     * Image: 0xE20722  61 08  bsr.b ADVANCE_INT  (0xE20724 + 0x08 = 0xE2072C)
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
     * TODO(source-mc3k, 0xE20722): restore `bsr.b ADVANCE_INT' once sau2.ld
     * places ec/advance_int.o after the ec/sau2 objects, in image order.
     */
    .short  0x6108                  /* bsr.b ADVANCE_INT */
    move.w  (%sp)+, %sr             /* Restore saved SR */
    rts
