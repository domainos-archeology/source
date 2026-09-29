/*
 * MMU_$REMOVE_ASID - Unlink every pageable page owned by an address space
 *
 * Original address: 0x00E23F0C, 118 bytes (0xE23F0C..0xE23F81).
 *
 * Image bytes (`gsk read 0xe23f0c 118`):
 *   00e23f0c  48 e7 37 30 20 39 00 e2  3c 90 2a 39 00 e2 3c 8c
 *   00e23f1c  9a 40 47 f9 00 ff b8 00  e5 48 d6 c0 3e 2f 00 20
 *   00e23f2c  48 c7 ee 9f 2c 3c fe 00  00 00 20 1b c0 86 b0 87
 *   00e23f3c  57 cd ff f8 66 3a 00 7c  07 00 30 3a fd e4 08 c0
 *   00e23f4c  00 01 33 c0 00 ff b4 00  20 23 c0 86 b0 87 66 0e
 *   00e23f5c  24 0b 94 bc 00 ff b8 00  e4 4a 61 00 fe 70 58 4b
 *   00e23f6c  33 fa fd be 00 ff b4 00  02 7c f8 ff 51 cd ff bc
 *   00e23f7c  4c df 0c ec 4e 75
 *
 * Argument: (0x20,SP) asid, word -> %d7, sign-extended and `ror.l #7' so
 * the compare against the PFT entry's top seven bits (%d6 = 0xfe000000)
 * needs no shift.  %a3 walks the PFT from MMAP_$LPPN (0xE23C90) for
 * MMAP_$HPPN - MMAP_$LPPN + 1 entries (`dbeq %d5' / `dbf %d5').  A match
 * raises IPL 7 outright (`ori #0x700,SR', no save), enables PTT access,
 * re-reads the entry, and if it still matches hands its ppn to
 * mmu_$remove_pmape (%d2 = (%a3 - PFT_BASE) >> 2); then CSR :=
 * MMU_$PID_PRIV and `andi #0xf8ff,SR' FORCES IPL 0 - not a restore.
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE23F46  move.w (-0x21c,PC),%d0  -> move.w MMU_$PID_PRIV,%d0   (+2)
 *   0xE23F66  bsr.w mmu_$remove_pmape -> jsr                        (+2)
 *   0xE23F6C  move.w (-0x242,PC),CSR  -> move.w MMU_$PID_PRIV,CSR   (+2)
 * `sub.l #imm,%d2' is the immediate-SOURCE form (94bc), spelled as .short.
 */

        .section ".text.MMU_$REMOVE_ASID","ax",@progbits
        .even

        .extern MMU_$GLOBALS
        .set    MMU_$PID_PRIV,  MMU_$GLOBALS + 0x0  /* map 0xE23D2C, a field of the MMU_$GLOBALS block */
        .extern MMAP_$DATA
        .equ    MMAP_$HPPN,     MMAP_$DATA + 0xA08  /* map 0xE23C8C, a field of the MMAP_$DATA block */
        .equ    MMAP_$LPPN,     MMAP_$DATA + 0xA0C  /* map 0xE23C90, a field of the MMAP_$DATA block */
        .equ    MMU_CSR,        0x00FFB400  /* SAU2 MMU CSR (hardware, SAU2_MMU_CSR in arch/m68k/sau2/hw.h) */
        .equ    PFT_BASE,       0x00FFB800  /* SAU2 page frame table (hardware, SAU2_PFT_BASE in arch/m68k/sau2/hw.h) */

        .globl  MMU_$REMOVE_ASID
        .globl  _MMU_$REMOVE_ASID
        .extern mmu_$remove_pmape

MMU_$REMOVE_ASID:
_MMU_$REMOVE_ASID:
        movem.l %d2-%d3/%d5-%d7/%a2-%a3,-(%sp) /* 0xE23F0C  48 e7 37 30 */
        move.l  MMAP_$LPPN,%d0          /* 0xE23F10  20 39 00 e2 3c 90 */
        move.l  MMAP_$HPPN,%d5          /* 0xE23F16  2a 39 00 e2 3c 8c */
        sub.w   %d0,%d5                 /* 0xE23F1C  9a 40             */
        lea     PFT_BASE,%a3            /* 0xE23F1E  47 f9 00 ff b8 00 */
        lsl.w   #2,%d0                  /* 0xE23F24  e5 48             */
        adda.w  %d0,%a3                 /* 0xE23F26  d6 c0             */
        move.w  (0x20,%sp),%d7          /* 0xE23F28  3e 2f 00 20       */
        ext.l   %d7                     /* 0xE23F2C  48 c7             */
        ror.l   #7,%d7                  /* 0xE23F2E  ee 9f             */
        move.l  #0xfe000000,%d6         /* 0xE23F30  2c 3c fe 00 00 00 */
1:      move.l  (%a3)+,%d0              /* 0xE23F36  20 1b             */
        and.l   %d6,%d0                 /* 0xE23F38  c0 86             */
        cmp.l   %d7,%d0                 /* 0xE23F3A  b0 87             */
        dbeq    %d5,1b                  /* 0xE23F3C  57 cd ff f8       */
        bne.s   9f                      /* 0xE23F40  66 3a             */
        ori.w   #0x700,%sr              /* 0xE23F42  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d0       /* 0xE23F46  30 3a fd e4 in the image */
        bset    #1,%d0                  /* 0xE23F4A  08 c0 00 01       */
        move.w  %d0,MMU_CSR             /* 0xE23F4E  33 c0 00 ff b4 00 */
        move.l  -(%a3),%d0              /* 0xE23F54  20 23             */
        and.l   %d6,%d0                 /* 0xE23F56  c0 86             */
        cmp.l   %d7,%d0                 /* 0xE23F58  b0 87             */
        bne.s   2f                      /* 0xE23F5A  66 0e             */
        move.l  %a3,%d2                 /* 0xE23F5C  24 0b             */
        .short  0x94bc                  /* 0xE23F5E  sub.l #0xffb800,%d2 */
        .long   0x00ffb800
        lsr.w   #2,%d2                  /* 0xE23F64  e4 4a             */
        jsr     mmu_$remove_pmape       /* 0xE23F66  61 00 fe 70 (bsr.w) in the image */
2:      addq.w  #4,%a3                  /* 0xE23F6A  58 4b             */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE23F6C  33 fa fd be 00 ff b4 00 in the image */
        andi.w  #0xf8ff,%sr             /* 0xE23F74  02 7c f8 ff       */
        dbf     %d5,1b                  /* 0xE23F78  51 cd ff bc       */
9:      movem.l (%sp)+,%d2-%d3/%d5-%d7/%a2-%a3 /* 0xE23F7C  4c df 0c ec */
        rts                             /* 0xE23F80  4e 75             */

        .size   MMU_$REMOVE_ASID, .-MMU_$REMOVE_ASID
