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

/*
 * Section note: gas fixes the pre-created .text section's alignment at 2**2
 * and offers no directive to lower it, which pads this object out to a
 * longword boundary and shifts the rest of the run off the image's gaps.  A
 * section created with `.section' starts at 2**0 and `.balign 2' raises it to
 * exactly the m68k requirement, so the four ec/sau2 objects link contiguously.
 * build/sau2/layout.ld (tools/gen_layout_ld.py) places each section by its
 * symbol's position in the SAU2 map (source-0ke7).
 */
        .section ".text.EC_$ADVANCE_WITHOUT_DISPATCH","ax",@progbits
        .balign 2

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
     * ec/sau2/advance_int.o follows this object directly (layout.ld, SAU2 map
     * PROC1_ASM order), and ADVANCE occupies the first four of those eight
     * bytes, so the R_68K_PC8 displacement is in range and gas keeps the
     * image's two-byte encoding: the object holds `61 00' plus the fixup and
     * is 0x10 bytes, exactly the image's 0xE20718..0xE20727 (source-mc3k,
     * source-0ke7).
     */
    bsr.b   ADVANCE_INT             /* Call internal advance */
    move.w  (%sp)+, %sr             /* Restore saved SR */
    rts
