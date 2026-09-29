/*
 * MMU_$SET_PROT - Replace a page's protection field, returning the old one
 *
 * Original address: 0x00E2422A, 48 bytes (0xE2422A..0xE24259).
 *
 * Image bytes (`gsk read 0xe2422a 48`):
 *   00e2422a  2f 03 36 2f 00 0c e9 4b  22 2f 00 08 e5 49 41 f9
 *   00e2423a  00 ff b8 00 40 e7 00 7c  07 00 30 30 10 00 c0 7c
 *   00e2424a  01 f0 b1 43 b7 70 10 00  46 df 26 1f e8 48 4e 75
 *
 * Arguments (after the pushed %d3): (8,SP) ppn longword, (0xC,SP) prot
 * word.  The protection lives in bits 4..8 of the FIRST word of the PFT
 * entry (the high half of the longword): under a saved-SR IPL-7 bracket
 * the field is read, and `eor'ed to the new value; the old field is
 * returned in %d0 shifted back down.  Byte-identical.
 * `and.w #imm,%d0' is the immediate-SOURCE form (c07c), spelled as .short.
 */

        .section ".text.MMU_$SET_PROT","ax",@progbits
        .even

        .equ    PFT_BASE,       0x00FFB800  /* SAU2 page frame table (hardware, SAU2_PFT_BASE in arch/m68k/sau2/hw.h) */

        .globl  MMU_$SET_PROT
        .globl  _MMU_$SET_PROT

MMU_$SET_PROT:
_MMU_$SET_PROT:
        move.l  %d3,-(%sp)              /* 0xE2422A  2f 03             */
        move.w  (0xc,%sp),%d3           /* 0xE2422C  36 2f 00 0c       */
        lsl.w   #4,%d3                  /* 0xE24230  e9 4b             */
        move.l  (8,%sp),%d1             /* 0xE24232  22 2f 00 08       */
        lsl.w   #2,%d1                  /* 0xE24236  e5 49             */
        lea     PFT_BASE,%a0            /* 0xE24238  41 f9 00 ff b8 00 */
        move.w  %sr,-(%sp)              /* 0xE2423E  40 e7             */
        ori.w   #0x700,%sr              /* 0xE24240  00 7c 07 00       */
        move.w  (0,%a0,%d1.w),%d0       /* 0xE24244  30 30 10 00       */
        .short  0xc07c, 0x01f0          /* 0xE24248  and.w #0x1f0,%d0  */
        eor.w   %d0,%d3                 /* 0xE2424C  b1 43             */
        eor.w   %d3,(0,%a0,%d1.w)       /* 0xE2424E  b7 70 10 00       */
        move.w  (%sp)+,%sr              /* 0xE24252  46 df             */
        move.l  (%sp)+,%d3              /* 0xE24254  26 1f             */
        lsr.w   #4,%d0                  /* 0xE24256  e8 48             */
        rts                             /* 0xE24258  4e 75             */

        .size   MMU_$SET_PROT, .-MMU_$SET_PROT
