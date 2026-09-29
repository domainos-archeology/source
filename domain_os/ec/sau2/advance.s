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

/*
 * Section note: gas fixes the pre-created .text section's alignment at 2**2
 * and offers no directive to lower it, which pads this object out to a
 * longword boundary and shifts the rest of the run off the image's gaps.  A
 * section created with `.section' starts at 2**0 and `.balign 2' raises it to
 * exactly the m68k requirement, so the four ec/sau2 objects link contiguously.
 * build/sau2/layout.ld (tools/gen_layout_ld.py) places each section by its
 * symbol's position in the SAU2 map (source-0ke7).
 */
        .section ".text.EC_$ADVANCE","ax",@progbits
        .balign 2

    .globl  EC_$ADVANCE
    .globl  _EC_$ADVANCE

EC_$ADVANCE:
_EC_$ADVANCE:
    ori.w   #0x0700, %sr            /* Disable interrupts (IPL = 7) */
    movea.l 4(%sp), %a0             /* Load eventcount pointer */
    /*
     * Image: 0xE206F6  61 34  bsr.b ADVANCE_INT  (0xE206F8 + 0x34 = 0xE2072C)
     *
     * The generated layout.ld links ec/sau2/advance_int.o (ADVANCE 0xE20728 / ADVANCE_INT
     * 0xE2072C / ADVANCE_ALL_INT 0xE207C6) immediately after the three
     * ec/sau2 entry objects, in the SAU2 map's PROC1_ASM order, so the
     * R_68K_PC8 displacement is in range and gas keeps the image's two-byte
     * encoding: the object holds `61 00' plus the fixup and is 0x14 bytes,
     * exactly the image's 0xE206EE..0xE20701 (source-mc3k, source-0ke7).
     */
    bsr.b   ADVANCE_INT             /* Call internal advance */
    bsr.w   PROC1_$DISPATCH_INT     /* Call dispatcher */
    andi.w  #0xF8FF, %sr            /* Restore interrupts (clear IPL) */
    rts
