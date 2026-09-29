/*
 * mmu_$remove_internal / mmu_$remove_pmape / mmu_$unlink_from_hash
 *
 * One 108-byte run, 0x00E23DCC..0x00E23E37, with three fall-through entry
 * points.  Register ABI throughout; never called from C.
 *
 *   0xE23DCC  mmu_$remove_internal  %d2 = ppn.  Sets %a3 = the PFT entry
 *                                   and falls into
 *   0xE23DD8  mmu_$remove_pmape     %d2 = ppn, %a3 = PFT entry.  %a2 = the
 *                                   PTT entry (PTT_BASE + ASID_TABLE[ppn]
 *                                   << 6), %d3 = *%a3, %d1 = 0, then into
 *   0xE23DF4  mmu_$unlink_from_hash %d2 = ppn, %d1 = predecessor's PFT
 *                                   byte offset (0 = unknown), %d3 = the
 *                                   entry value, %a2 = PTT entry, %a3 =
 *                                   PFT entry.
 *
 * Callers: MMU_$REMOVE / MMU_$REMOVE_LIST `bsr.b' the first (0xE23D80,
 * 0xE23DB6); MMU_$REMOVE_ASID and mmu_$installi `bsr.w' the second
 * (0xE23F66, 0xE240B2); MMU_$REMOVE_VIRTUAL `bsr.w' the third (0xE23EBA)
 * with a known predecessor in %d1.
 *
 * Image bytes (`gsk read 0xe23dcc 108`):
 *   00e23dcc  30 02 e5 48 47 f9 00 ff  b8 00 d6 c0 42 80 45 f9
 *   00e23ddc  00 ec 28 00 d4 c2 d4 c2  30 12 45 f9 00 70 00 00
 *   00e23dec  ed 88 26 13 d5 c0 72 00  30 03 c0 7c 0f ff 67 3a
 *   00e23dfc  b0 42 67 2c 41 f9 00 ff  b8 00 4a 41 66 10 32 00
 *   00e23e0c  e5 49 30 30 10 02 c0 7c  0f ff b0 42 66 f0 30 30
 *   00e23e1c  10 02 08 80 00 0f b7 40  c0 7c 8f ff b1 70 10 02
 *   00e23e2c  e4 49 34 81 02 93 00 00  60 00 4e 75
 *
 * mmu_$unlink_from_hash:
 *   link = entry & 0xfff; link == 0 -> nothing is written (0xE23DFA)
 *   link == ppn (a one-entry ring) -> skip the predecessor fix-up
 *   %d1 == 0 -> walk the ring from `link' until the entry whose link is
 *               ppn; %d1 = that entry's byte offset (0xE23E0A-0xE23E18)
 *   predecessor's low word ^= ((pred & ~0x8000) ^ entry_low) & 0x8fff
 *               (0xE23E1A-0xE23E28): it inherits ppn's link and the
 *               head bit becomes pred15 ^ entry15
 *   PTT entry := %d1 >> 2 (the predecessor, or 0), and the removed
 *               entry keeps only bits 13 and 14 (0xE23E2C-0xE23E30)
 *
 * Byte-identical to the image: every operand is absolute or local.  The
 * `and.w #imm,%d0' instructions are the immediate-SOURCE form (c07c),
 * spelled as .short because gas would pick andi (0240).
 */

        .section ".text.mmu_$remove_internal","ax",@progbits
        .even

        .equ    PFT_BASE,       0x00FFB800  /* SAU2 page frame table (hardware) */
        .equ    ASID_TABLE,     0x00EC2800  /* map MMU_$PTTX in OS_PMAPS, not yet an object: TODO(source-o56c) */
        .equ    PTT_BASE,       0x00700000  /* SAU2 page translation table window (hardware) */

        .globl  mmu_$remove_internal
        .globl  _mmu_$remove_internal
        .globl  mmu_$remove_pmape
        .globl  _mmu_$remove_pmape
        .globl  mmu_$unlink_from_hash
        .globl  _mmu_$unlink_from_hash

mmu_$remove_internal:
_mmu_$remove_internal:
        move.w  %d2,%d0                 /* 0xE23DCC  30 02             */
        lsl.w   #2,%d0                  /* 0xE23DCE  e5 48             */
        lea     PFT_BASE,%a3            /* 0xE23DD0  47 f9 00 ff b8 00 */
        adda.w  %d0,%a3                 /* 0xE23DD6  d6 c0             */
mmu_$remove_pmape:
_mmu_$remove_pmape:
        clr.l   %d0                     /* 0xE23DD8  42 80             */
        lea     ASID_TABLE,%a2          /* 0xE23DDA  45 f9 00 ec 28 00 */
        adda.w  %d2,%a2                 /* 0xE23DE0  d4 c2             */
        adda.w  %d2,%a2                 /* 0xE23DE2  d4 c2             */
        move.w  (%a2),%d0               /* 0xE23DE4  30 12             */
        lea     PTT_BASE,%a2            /* 0xE23DE6  45 f9 00 70 00 00 */
        lsl.l   #6,%d0                  /* 0xE23DEC  ed 88             */
        move.l  (%a3),%d3               /* 0xE23DEE  26 13             */
        adda.l  %d0,%a2                 /* 0xE23DF0  d5 c0             */
        moveq   #0,%d1                  /* 0xE23DF2  72 00             */
mmu_$unlink_from_hash:
_mmu_$unlink_from_hash:
        move.w  %d3,%d0                 /* 0xE23DF4  30 03             */
        .short  0xc07c, 0x0fff          /* 0xE23DF6  and.w #0xfff,%d0  */
        beq.s   9f                      /* 0xE23DFA  67 3a             */
        cmp.w   %d2,%d0                 /* 0xE23DFC  b0 42             */
        beq.s   8f                      /* 0xE23DFE  67 2c             */
        lea     PFT_BASE,%a0            /* 0xE23E00  41 f9 00 ff b8 00 */
        tst.w   %d1                     /* 0xE23E06  4a 41             */
        bne.s   7f                      /* 0xE23E08  66 10             */
6:      move.w  %d0,%d1                 /* 0xE23E0A  32 00             */
        lsl.w   #2,%d1                  /* 0xE23E0C  e5 49             */
        move.w  (2,%a0,%d1.w),%d0       /* 0xE23E0E  30 30 10 02       */
        .short  0xc07c, 0x0fff          /* 0xE23E12  and.w #0xfff,%d0  */
        cmp.w   %d2,%d0                 /* 0xE23E16  b0 42             */
        bne.s   6b                      /* 0xE23E18  66 f0             */
7:      move.w  (2,%a0,%d1.w),%d0       /* 0xE23E1A  30 30 10 02       */
        bclr    #15,%d0                 /* 0xE23E1E  08 80 00 0f       */
        eor.w   %d3,%d0                 /* 0xE23E22  b7 40             */
        .short  0xc07c, 0x8fff          /* 0xE23E24  and.w #0x8fff,%d0 */
        eor.w   %d0,(2,%a0,%d1.w)       /* 0xE23E28  b1 70 10 02       */
8:      lsr.w   #2,%d1                  /* 0xE23E2C  e4 49             */
        move.w  %d1,(%a2)               /* 0xE23E2E  34 81             */
        andi.l  #0x6000,(%a3)           /* 0xE23E30  02 93 00 00 60 00 */
9:      rts                             /* 0xE23E36  4e 75             */

        .size   mmu_$remove_internal, .-mmu_$remove_internal
