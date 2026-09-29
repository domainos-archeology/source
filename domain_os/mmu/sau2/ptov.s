/*
 * MMU_$PTOV - Physical page number to virtual address
 *
 * Original address: 0x00E241B8, 60 bytes (0xE241B8..0xE241F3).
 *
 * Image bytes (`gsk read 0xe241b8 60`):
 *   00e241b8  22 2f 00 04 e5 49 41 f9  00 ff b8 00 20 30 10 00
 *   00e241c8  c0 7c 0f ff 67 22 41 f9  00 ec 28 00 e2 49 c0 bc
 *   00e241d8  00 0f 00 00 30 30 10 00  12 3a fb 4c 67 04 ed 88
 *   00e241e8  4e 75 e5 48 e9 88 4e 75  42 80 4e 75
 *
 * Argument: (4,SP) ppn, longword -> %d1; %d1.w = ppn << 2 indexes the PFT
 * (sign-extended), %d1.w >> 1 MMU_$PTTX.  An entry with no link
 * returns 0.  Otherwise %d0 = (entry & 0xf0000) | MMU_$PTTX[ppn] and:
 *   68020 (HIGH byte of M68020, `move.b'): return %d0 << 6
 *   68010: %d0.w <<= 2 (a WORD shift), return %d0 << 4
 *
 * Deviation from the image bytes, forced by separate assembly:
 *   0xE241E0  move.b (-0x4b4,PC),%d1  -> move.b M68020,%d1          (+2)
 * `and.w #imm' / `and.l #imm' are the immediate-SOURCE forms (c07c /
 * c0bc), spelled as .short.
 */

        .section ".text.MMU_$PTOV","ax",@progbits
        .even

        .extern MMU_$GLOBALS
        .set    M68020,         MMU_$GLOBALS + 0x2  /* map 0xE23D2E, a field of the MMU_$GLOBALS block */
        .equ    PFT_BASE,       0x00FFB800  /* SAU2 page frame table (hardware, SAU2_PFT_BASE in arch/m68k/sau2/hw.h) */
        .extern MMU_$PTTX               /* map MMU_$PTTX 0xEC2800 in OS_PMAPS, a MODULE_DATA block */

        .globl  MMU_$PTOV
        .globl  _MMU_$PTOV

MMU_$PTOV:
_MMU_$PTOV:
        move.l  (4,%sp),%d1             /* 0xE241B8  22 2f 00 04       */
        lsl.w   #2,%d1                  /* 0xE241BC  e5 49             */
        lea     PFT_BASE,%a0            /* 0xE241BE  41 f9 00 ff b8 00 */
        move.l  (0,%a0,%d1.w),%d0       /* 0xE241C4  20 30 10 00       */
        .short  0xc07c, 0x0fff          /* 0xE241C8  and.w #0xfff,%d0  */
        beq.s   9f                      /* 0xE241CC  67 22             */
        lea     MMU_$PTTX,%a0           /* 0xE241CE  41 f9 00 ec 28 00 */
        lsr.w   #1,%d1                  /* 0xE241D4  e2 49             */
        .short  0xc0bc                  /* 0xE241D6  and.l #0xf0000,%d0 */
        .long   0x000f0000
        move.w  (0,%a0,%d1.w),%d0       /* 0xE241DC  30 30 10 00       */
        move.b  M68020,%d1              /* 0xE241E0  12 3a fb 4c in the image */
        beq.s   1f                      /* 0xE241E4  67 04             */
        lsl.l   #6,%d0                  /* 0xE241E6  ed 88             */
        rts                             /* 0xE241E8  4e 75             */
1:      lsl.w   #2,%d0                  /* 0xE241EA  e5 48             */
        lsl.l   #4,%d0                  /* 0xE241EC  e9 88             */
        rts                             /* 0xE241EE  4e 75             */
9:      clr.l   %d0                     /* 0xE241F0  42 80             */
        rts                             /* 0xE241F2  4e 75             */

        .size   MMU_$PTOV, .-MMU_$PTOV
