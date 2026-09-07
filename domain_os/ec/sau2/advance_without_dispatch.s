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
     * sau2.ld links ec/advance_int.o immediately after the three ec/sau2
     * objects (SAU2 map PROC1_ASM order), so the R_68K_PC8 displacement is
     * in range and gas keeps the image's two-byte encoding: the object holds
     * `61 00' plus the fixup and is 0x10 bytes, exactly the image's
     * 0xE20718..0xE20727 (source-mc3k).
     *
     * The linked displacement is 0x04, not the image's 0x08.
     * TODO(source-0ke7, 0xE20728): ADVANCE, the 4-byte C-callable entry that
     * falls through into ADVANCE_INT, is not in the tree yet, so
     * ec/advance_int.o starts directly at ADVANCE_INT.
     */
    bsr.b   ADVANCE_INT             /* Call internal advance */
    move.w  (%sp)+, %sr             /* Restore saved SR */
    rts
