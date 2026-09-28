/*
 * MMU_$REMOVE_VIRTUAL - Unlink an address space's pages at a run of VAs
 *
 * Original address: 0x00E23E38, 212 bytes (0xE23E38..0xE23F0B).
 *
 * Image bytes (`gsk read 0xe23e38 212`):
 *   00e23e38  48 e7 3f 38 47 f9 00 ff  b8 00 28 2f 00 28 20 04
 *   00e23e48  c0 ba fe e6 d0 bc 00 70  00 00 24 40 30 3a fe de
 *   00e23e58  e1 ac 38 2f 00 2e ee 9c  48 44 28 6f 00 30 3e 2f
 *   00e23e68  00 2c 53 47 42 82 40 c6  00 7c 07 00 30 3a fe b6
 *   00e23e78  08 c0 00 01 33 c0 00 ff  b4 00 47 f9 00 ff b8 00
 *   00e23e88  34 12 c4 7c 0f ff 67 3c  3a 02 72 00 e5 4a 26 33
 *   00e23e98  20 00 20 03 48 40 b9 40  c0 7c fe 0f 67 0e 32 02
 *   00e23ea8  34 03 c4 7c 0f ff b4 45  66 e2 60 18 47 f3 20 00
 *   00e23eb8  e4 4a 61 00 ff 38 c4 bc  00 00 ff ff 28 c2 47 f9
 *   00e23ec8  00 ff b8 00 45 ea 04 00  b5 fc 00 80 00 00 6d 08
 *   00e23ed8  52 84 24 7c 00 70 00 00  30 07 c0 7c 00 1f 57 cf
 *   00e23ee8  ff a0 33 fa fe 40 00 ff  b4 00 46 c6 51 cf ff 7a
 *   00e23ef8  20 0c 90 af 00 30 20 6f  00 34 e4 88 30 80 4c df
 *   00e23f08  1c fc 4e 75
 *
 * Arguments (after the 0x24-byte movem save):
 *   (0x28,SP)  va             longword -> %d4 / the PTT entry %a2
 *   (0x2C,SP)  count          word -> %d7 minus one
 *   (0x2E,SP)  asid           word, becomes the low word of %d4 before
 *                             `ror.l #7' / `swap'
 *   (0x30,SP)  ppn_array      -> %a4, one longword written per page found
 *   (0x34,SP)  removed_count  -> word, (%a4 - array) / 4 at the end
 *
 * %d4.w is the match key: the high word of ((va << MMU_$VA_SHIFT) with
 * asid in the low word) rotated right 7.  For each PTT entry: if the
 * chain is non-empty, walk it (`%d1' = previous entry's byte offset, 0 at
 * the head) until an entry whose high word matches under 0xfe0f; that
 * entry is handed to mmu_$unlink_from_hash (%d2 = ppn, %d1, %d3 = value,
 * %a2, %a3 = its PFT entry) and its ppn appended to the array.  The PTT
 * pointer advances 0x400 and wraps from 0x800000 to 0x700000 with %d4
 * incremented.  Pages are taken in groups: the inner `dbeq' runs while
 * (%d7 & 0x1f) != 0, the CSR/SR bracket is dropped between groups, and
 * the outer `dbf' continues until %d7 wraps.
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE23E48  and.l (-0x11a,PC),%d0   -> and.l VA_TO_PTT_OFFSET_MASK (+2)
 *   0xE23E54  move.w (-0x122,PC),%d0  -> move.w MMU_$VA_SHIFT,%d0    (+2)
 *   0xE23E74  move.w (-0x14a,PC),%d0  -> move.w MMU_$PID_PRIV,%d0    (+2)
 *   0xE23EBA  bsr.w mmu_$unlink_from_hash -> jsr                    (+2)
 *   0xE23EEA  move.w (-0x1c0,PC),CSR  -> move.w MMU_$PID_PRIV,CSR    (+2)
 * Immediate-source `and'/`add' forms are spelled as .short.
 */

        .text
        .even

        .equ    MMU_$PID_PRIV,  0x00E23D2C
        .equ    VA_TO_PTT_OFFSET_MASK, 0x00E23D30
        .equ    MMU_$VA_SHIFT,  0x00E23D34
        .equ    MMU_CSR,        0x00FFB400
        .equ    PFT_BASE,       0x00FFB800

        .globl  MMU_$REMOVE_VIRTUAL
        .globl  _MMU_$REMOVE_VIRTUAL
        .extern mmu_$unlink_from_hash

MMU_$REMOVE_VIRTUAL:
_MMU_$REMOVE_VIRTUAL:
        movem.l %d2-%d7/%a2-%a4,-(%sp)  /* 0xE23E38  48 e7 3f 38       */
        lea     PFT_BASE,%a3            /* 0xE23E3C  47 f9 00 ff b8 00 */
        move.l  (0x28,%sp),%d4          /* 0xE23E42  28 2f 00 28       */
        move.l  %d4,%d0                 /* 0xE23E46  20 04             */
        and.l   VA_TO_PTT_OFFSET_MASK,%d0 /* 0xE23E48  c0 ba fe e6 in the image */
        .short  0xd0bc                  /* 0xE23E4C  add.l #0x700000,%d0 */
        .long   0x00700000
        movea.l %d0,%a2                 /* 0xE23E52  24 40             */
        move.w  MMU_$VA_SHIFT,%d0       /* 0xE23E54  30 3a fe de in the image */
        lsl.l   %d0,%d4                 /* 0xE23E58  e1 ac             */
        move.w  (0x2e,%sp),%d4          /* 0xE23E5A  38 2f 00 2e       */
        ror.l   #7,%d4                  /* 0xE23E5E  ee 9c             */
        swap    %d4                     /* 0xE23E60  48 44             */
        movea.l (0x30,%sp),%a4          /* 0xE23E62  28 6f 00 30       */
        move.w  (0x2c,%sp),%d7          /* 0xE23E66  3e 2f 00 2c       */
        subq.w  #1,%d7                  /* 0xE23E6A  53 47             */
        clr.l   %d2                     /* 0xE23E6C  42 82             */
        move.w  %sr,%d6                 /* 0xE23E6E  40 c6             */
1:      ori.w   #0x700,%sr              /* 0xE23E70  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d0       /* 0xE23E74  30 3a fe b6 in the image */
        bset    #1,%d0                  /* 0xE23E78  08 c0 00 01       */
        move.w  %d0,MMU_CSR             /* 0xE23E7C  33 c0 00 ff b4 00 */
        lea     PFT_BASE,%a3            /* 0xE23E82  47 f9 00 ff b8 00 */
2:      move.w  (%a2),%d2               /* 0xE23E88  34 12             */
        .short  0xc47c, 0x0fff          /* 0xE23E8A  and.w #0xfff,%d2  */
        beq.s   6f                      /* 0xE23E8E  67 3c             */
        move.w  %d2,%d5                 /* 0xE23E90  3a 02             */
        moveq   #0,%d1                  /* 0xE23E92  72 00             */
3:      lsl.w   #2,%d2                  /* 0xE23E94  e5 4a             */
        move.l  (0,%a3,%d2.w),%d3       /* 0xE23E96  26 33 20 00       */
        move.l  %d3,%d0                 /* 0xE23E9A  20 03             */
        swap    %d0                     /* 0xE23E9C  48 40             */
        eor.w   %d4,%d0                 /* 0xE23E9E  b9 40             */
        .short  0xc07c, 0xfe0f          /* 0xE23EA0  and.w #0xfe0f,%d0 */
        beq.s   5f                      /* 0xE23EA4  67 0e             */
        move.w  %d2,%d1                 /* 0xE23EA6  32 02             */
        move.w  %d3,%d2                 /* 0xE23EA8  34 03             */
        .short  0xc47c, 0x0fff          /* 0xE23EAA  and.w #0xfff,%d2  */
        cmp.w   %d5,%d2                 /* 0xE23EAE  b4 45             */
        bne.s   3b                      /* 0xE23EB0  66 e2             */
        bra.s   6f                      /* 0xE23EB2  60 18             */
5:      lea     (0,%a3,%d2.w),%a3       /* 0xE23EB4  47 f3 20 00       */
        lsr.w   #2,%d2                  /* 0xE23EB8  e4 4a             */
        jsr     mmu_$unlink_from_hash   /* 0xE23EBA  61 00 ff 38 (bsr.w) in the image */
        .short  0xc4bc                  /* 0xE23EBE  and.l #0xffff,%d2 */
        .long   0x0000ffff
        move.l  %d2,(%a4)+              /* 0xE23EC4  28 c2             */
        lea     PFT_BASE,%a3            /* 0xE23EC6  47 f9 00 ff b8 00 */
6:      lea     (0x400,%a2),%a2         /* 0xE23ECC  45 ea 04 00       */
        cmpa.l  #0x800000,%a2           /* 0xE23ED0  b5 fc 00 80 00 00 */
        blt.s   7f                      /* 0xE23ED6  6d 08             */
        addq.l  #1,%d4                  /* 0xE23ED8  52 84             */
        movea.l #0x700000,%a2           /* 0xE23EDA  24 7c 00 70 00 00 */
7:      move.w  %d7,%d0                 /* 0xE23EE0  30 07             */
        .short  0xc07c, 0x001f          /* 0xE23EE2  and.w #0x1f,%d0   */
        dbeq    %d7,2b                  /* 0xE23EE6  57 cf ff a0       */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE23EEA  33 fa fe 40 00 ff b4 00 in the image */
        move.w  %d6,%sr                 /* 0xE23EF2  46 c6             */
        dbf     %d7,1b                  /* 0xE23EF4  51 cf ff 7a       */
        move.l  %a4,%d0                 /* 0xE23EF8  20 0c             */
        sub.l   (0x30,%sp),%d0          /* 0xE23EFA  90 af 00 30       */
        movea.l (0x34,%sp),%a0          /* 0xE23EFE  20 6f 00 34       */
        lsr.l   #2,%d0                  /* 0xE23F02  e4 88             */
        move.w  %d0,(%a0)               /* 0xE23F04  30 80             */
        movem.l (%sp)+,%d2-%d7/%a2-%a4  /* 0xE23F06  4c df 1c fc       */
        rts                             /* 0xE23F0A  4e 75             */

        .size   MMU_$REMOVE_VIRTUAL, .-MMU_$REMOVE_VIRTUAL
