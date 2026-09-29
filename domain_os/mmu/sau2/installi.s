/*
 * mmu_$installi - Link a physical page into its PTT hash chain
 *
 * Original address: 0x00E2409C, 114 bytes (0xE2409C..0xE2410D).  Reached
 * by `bsr' from MMU_$INSTALL (0xE2408A), MMU_$INSTALL_LIST (0xE2402A) and
 * MMU_$INSTALL_PRIVATE (0xE23FC4); never called from C (bead source-w78q).
 *
 * Image bytes (`gsk read 0xe2409c 114`):
 *   00e2409c  47 f9 00 ff b8 00 20 02  e5 48 d6 c0 30 2b 00 02
 *   00e240ac  c0 7c 0f ff 67 04 61 00  fd 24 41 f9 00 ec 28 00
 *   00e240bc  d0 c2 d0 c2 30 84 42 44  20 3c fe 00 00 00 c0 84
 *   00e240cc  66 04 08 c4 00 0c 20 0c  c0 ba fc 5a d0 bc 00 70
 *   00e240dc  00 00 24 40 30 12 c0 7c  0f ff 67 1a 41 f9 00 ff
 *   00e240ec  b8 02 e5 48 d0 c0 30 10  c0 7c 0f ff b1 44 b9 93
 *   00e240fc  b5 40 b1 50 60 0a 08 c4  00 0f b5 44 b9 93 34 82
 *   00e2410c  4e 75
 *
 * Register ABI (the caller has IPL 7 and PTT access enabled):
 *   %d2 = ppn (word), %a4 = va, %d4 = the packed word pair: high word =
 *   asid (bits 9..15) and protection << 4, low word = the va bits that
 *   select the PTT slot, (va & VA_TO_PTT_OFFSET_MASK) >> 6, low nibble
 *   clear (MMU_$INSTALL's shift-and-rotate packing, 0xE24054-0xE24074).
 * Leaves %a3 = the page's PFT entry (MMU_$INSTALL_PRIVATE relies on it).
 *
 *   0xE2409C-0xE240B4  if the page's PFT entry has a link, unlink it first
 *                      (mmu_$remove_pmape with %d2)
 *   0xE240B6-0xE240C0  MMU_$PTTX[ppn] := the low word of %d4 (`move.w %d4,(%a0)')
 *   0xE240C2-0xE240D0  %d4 low word cleared; if the asid field (bits
 *                      25..31) is zero the GLOBAL bit (12) is set
 *   0xE240D2-0xE240E6  %a2 = the PTT entry for va
 *   0xE240E8-0xE24100  chain non-empty: splice ppn in after the head,
 *                      copying the head's link, and repoint the head at
 *                      ppn (the three `eor' pairs)
 *   0xE24102-0xE2410A  chain empty: ppn becomes the HEAD (bit 15) linked
 *                      to itself, and the PTT entry names it
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE240B2  bsr.w mmu_$remove_pmape          -> jsr                 (+2)
 *   0xE240B0  beq.b +4 therefore assembles as beq.b +6
 *   0xE240D4  and.l (-0x3a6,PC),%d0            -> and.l VA_TO_PTT_OFFSET_MASK (+2)
 * `and.w #imm' / `and.l #imm' / `add.l #imm' are the immediate-SOURCE
 * forms (c07c / c0bc / d0bc); gas would pick the andi/addi opcodes, so
 * they are spelled as .short.
 */

        .section ".text.mmu_$installi","ax",@progbits
        .even

        .extern MMU_$GLOBALS
        .set    VA_TO_PTT_OFFSET_MASK, MMU_$GLOBALS + 0x4  /* map 0xE23D30, a field of the MMU_$GLOBALS block */
        .equ    PFT_BASE,       0x00FFB800  /* SAU2 page frame table (hardware, SAU2_PFT_BASE in arch/m68k/sau2/hw.h) */
        .extern MMU_$PTTX               /* map MMU_$PTTX 0xEC2800 in OS_PMAPS, a MODULE_DATA block */

        .globl  mmu_$installi
        .globl  _mmu_$installi
        .extern mmu_$remove_pmape

mmu_$installi:
_mmu_$installi:
        lea     PFT_BASE,%a3            /* 0xE2409C  47 f9 00 ff b8 00 */
        move.l  %d2,%d0                 /* 0xE240A2  20 02             */
        lsl.w   #2,%d0                  /* 0xE240A4  e5 48             */
        adda.w  %d0,%a3                 /* 0xE240A6  d6 c0             */
        move.w  (2,%a3),%d0             /* 0xE240A8  30 2b 00 02       */
        .short  0xc07c, 0x0fff          /* 0xE240AC  and.w #0xfff,%d0  */
        beq.s   1f                      /* 0xE240B0  67 04 in the image */
        jsr     mmu_$remove_pmape       /* 0xE240B2  61 00 fd 24 (bsr.w) in the image */
1:      lea     MMU_$PTTX,%a0           /* 0xE240B6  41 f9 00 ec 28 00 */
        adda.w  %d2,%a0                 /* 0xE240BC  d0 c2             */
        adda.w  %d2,%a0                 /* 0xE240BE  d0 c2             */
        move.w  %d4,(%a0)               /* 0xE240C0  30 84             */
        clr.w   %d4                     /* 0xE240C2  42 44             */
        move.l  #0xfe000000,%d0         /* 0xE240C4  20 3c fe 00 00 00 */
        and.l   %d4,%d0                 /* 0xE240CA  c0 84             */
        bne.s   2f                      /* 0xE240CC  66 04             */
        bset    #12,%d4                 /* 0xE240CE  08 c4 00 0c       */
2:      move.l  %a4,%d0                 /* 0xE240D2  20 0c             */
        and.l   VA_TO_PTT_OFFSET_MASK,%d0 /* 0xE240D4  c0 ba fc 5a in the image */
        .short  0xd0bc                  /* 0xE240D8  add.l #0x700000,%d0 */
        .long   0x00700000
        movea.l %d0,%a2                 /* 0xE240DE  24 40             */
        move.w  (%a2),%d0               /* 0xE240E0  30 12             */
        .short  0xc07c, 0x0fff          /* 0xE240E2  and.w #0xfff,%d0  */
        beq.s   3f                      /* 0xE240E6  67 1a             */
        lea     PFT_BASE+2,%a0          /* 0xE240E8  41 f9 00 ff b8 02 */
        lsl.w   #2,%d0                  /* 0xE240EE  e5 48             */
        adda.w  %d0,%a0                 /* 0xE240F0  d0 c0             */
        move.w  (%a0),%d0               /* 0xE240F2  30 10             */
        .short  0xc07c, 0x0fff          /* 0xE240F4  and.w #0xfff,%d0  */
        eor.w   %d0,%d4                 /* 0xE240F8  b1 44             */
        eor.l   %d4,(%a3)               /* 0xE240FA  b9 93             */
        eor.w   %d2,%d0                 /* 0xE240FC  b5 40             */
        eor.w   %d0,(%a0)               /* 0xE240FE  b1 50             */
        bra.s   4f                      /* 0xE24100  60 0a             */
3:      bset    #15,%d4                 /* 0xE24102  08 c4 00 0f       */
        eor.w   %d2,%d4                 /* 0xE24106  b5 44             */
        eor.l   %d4,(%a3)               /* 0xE24108  b9 93             */
        move.w  %d2,(%a2)               /* 0xE2410A  34 82             */
4:      rts                             /* 0xE2410C  4e 75             */

        .size   mmu_$installi, .-mmu_$installi
