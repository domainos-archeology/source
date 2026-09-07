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
     * sau2.ld links ec/advance_int.o immediately after the three ec/sau2
     * objects (SAU2 map PROC1_ASM order), so the R_68K_PC8 displacement is
     * in range and gas keeps the image's two-byte encoding: the object holds
     * `61 00' plus the fixup and is 0x14 bytes, exactly the image's
     * 0xE206EE..0xE20701 (source-mc3k).
     *
     * The linked displacement is 0x32, not the image's 0x34, for two reasons,
     * neither of them an instruction-byte difference:
     *   -4  TODO(source-0ke7, 0xE20728): ADVANCE, the 4-byte C-callable entry
     *       that falls through into ADVANCE_INT, is not in the tree yet, so
     *       ec/advance_int.o starts directly at ADVANCE_INT.
     *   +2  gas gives every .s .text section 2**2 alignment, so a 2-byte pad
     *       follows advance_all.o (image size 0x16, ending 2-mod-4).
     */
    bsr.b   ADVANCE_INT             /* Call internal advance */
    bsr.w   PROC1_$DISPATCH_INT     /* Call dispatcher */
    andi.w  #0xF8FF, %sr            /* Restore interrupts (clear IPL) */
    rts
