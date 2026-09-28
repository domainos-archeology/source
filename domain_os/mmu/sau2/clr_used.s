/*
 * MMU_$CLR_USED - Clear the REFERENCED bit of a physical page's PFT entry
 *
 * Original address: 0x00E2425A, 20 bytes.  Callers: 0x00E0A300, 0x00E0F368.
 *
 * Image bytes (`gsk read 0xe2425a 20`):
 *   00e2425a  41 f9 00 ff b8 00 30 2f  00 04 e5 48 02 70 df ff
 *   00e2426a  00 02 4e 75
 *
 * Hand-written assembly: no link/unlk, the argument is read straight off
 * the stack at (4,%sp) as a WORD (the low half of the pushed longword ppn),
 * and the PFT (0xFFB800, 4 bytes per page) is indexed by ppn*4.  The
 * `andi.w #0xdfff' clears bit 13 of the entry's second word, the
 * PMAPE_FLAG_REFERENCED bit that MMAP_$WS_SCAN and mmap_$trim_wsl test.
 */

        .text
        .even

        .equ    PFT_BASE, 0x00FFB800

        .globl  MMU_$CLR_USED
        .globl  _MMU_$CLR_USED

MMU_$CLR_USED:
_MMU_$CLR_USED:
        lea     PFT_BASE,%a0            /* 0xE2425A  41 f9 00 ff b8 00 */
        move.w  (4,%sp),%d0             /* 0xE24260  30 2f 00 04       */
        lsl.w   #2,%d0                  /* 0xE24264  e5 48             */
        andi.w  #0xdfff,(2,%a0,%d0.w)   /* 0xE24266  02 70 df ff 00 02 */
        rts                             /* 0xE2426C  4e 75             */
