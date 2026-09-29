/*
 * MMU_$VTOP - Virtual address to physical page number
 *
 * Byte gate (source-6psc; tools/asm_compare.py, `make check'): encodings
 * identical to the image (modulo the documented widenings); address
 * operands resolve to our objects.
 *
 * Original address: 0x00E2410E, 170 bytes (0xE2410E..0xE241B7).
 *
 * Image bytes (`gsk read 0xe2410e 170`):
 *   00e2410e  48 e7 1c 00 2a 2f 00 10  22 05 c2 ba fc 16 d2 bc
 *   00e2411e  00 70 00 00 22 41 30 3a  fc 0e e1 ad 3a 39 00 e2
 *   00e2412e  06 0a ee 9d 48 45 40 e7  00 7c 07 00 32 3a fb f0
 *   00e2413e  08 c1 00 01 33 c1 00 ff  b4 00 30 11 c0 bc 00 00
 *   00e2414e  0f ff 67 32 e5 48 38 00  41 f9 00 ff b8 00 26 30
 *   00e2415e  00 00 22 03 48 41 bb 41  c2 7c fe 0f 67 34 c2 7c
 *   00e2416e  00 0f 66 06 08 03 00 0c  66 28 30 03 c0 7c 0f ff
 *   00e2417e  e5 48 b0 44 66 d8 70 00  33 fa fb a4 00 ff b4 00
 *   00e2418e  46 df 20 6f 00 14 20 bc  00 07 00 01 4c df 00 38
 *   00e2419e  4e 75 e4 48 33 fa fb 88  00 ff b4 00 46 df 20 6f
 *   00e241ae  00 14 42 90 4c df 00 38  4e 75
 *
 * Arguments (after the 12-byte movem save):
 *   (0x10,SP)  va      longword -> %d5 / the PTT entry %a1
 *   (0x14,SP)  status  -> status_$t: 0 on a hit, 0x00070001 on a miss
 * Result: %d0 = the ppn, or 0 on a miss.
 *
 * %d5.w is the match key: high word of ((va << MMU_$VA_SHIFT) with
 * PROC1_$AS_ID (0xE2060A) in the low word) rotated right 7.  Under a
 * saved-SR IPL-7 bracket with PTT access on, the chain at the PTT entry
 * is walked: an entry whose high word equals the key under 0xfe0f is a
 * hit, and so is one that matches under 0x000f and has bit 12 (GLOBAL)
 * set; the walk ends when the link comes back to the head.
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE24118  and.l (-0x3ea,PC),%d1   -> and.l VA_TO_PTT_OFFSET_MASK (+2)
 *   0xE24124  move.w (-0x3f2,PC),%d0  -> move.w MMU_$VA_SHIFT,%d0    (+2)
 *   0xE2413A  move.w (-0x410,PC),%d1  -> move.w MMU_$PID_PRIV,%d1    (+2)
 *   0xE24186  move.w (-0x45c,PC),CSR  -> move.w MMU_$PID_PRIV,CSR    (+2)
 *   0xE241A2  move.w (-0x478,PC),CSR  -> move.w MMU_$PID_PRIV,CSR    (+2)
 * Immediate-source `and'/`add' forms are spelled as .short.
 */

        .section ".text.MMU_$VTOP","ax",@progbits
        .even

        .extern MMU_$GLOBALS
        .set    MMU_$PID_PRIV,  MMU_$GLOBALS + 0x0  /* map 0xE23D2C, a field of the MMU_$GLOBALS block */
        .set    VA_TO_PTT_OFFSET_MASK, MMU_$GLOBALS + 0x4  /* map 0xE23D30, a field of the MMU_$GLOBALS block */
        .set    MMU_$VA_SHIFT,  MMU_$GLOBALS + 0x8  /* map 0xE23D34, a field of the MMU_$GLOBALS block */
        .extern PROC1_$AS_ID            /* uint16_t, proc1/proc1_data.c, map 0xE2060A (source-6psc) */
        .equ    MMU_CSR,        0x00FFB400  /* SAU2 MMU CSR (hardware, SAU2_MMU_CSR in arch/m68k/sau2/hw.h) */
        .equ    PFT_BASE,       0x00FFB800  /* SAU2 page frame table (hardware, SAU2_PFT_BASE in arch/m68k/sau2/hw.h) */

        .globl  MMU_$VTOP
        .globl  _MMU_$VTOP

MMU_$VTOP:
_MMU_$VTOP:
        movem.l %d3-%d5,-(%sp)          /* 0xE2410E  48 e7 1c 00       */
        move.l  (0x10,%sp),%d5          /* 0xE24112  2a 2f 00 10       */
        move.l  %d5,%d1                 /* 0xE24116  22 05             */
        and.l   VA_TO_PTT_OFFSET_MASK,%d1 /* 0xE24118  c2 ba fc 16 in the image */
        .short  0xd2bc                  /* 0xE2411C  add.l #0x700000,%d1 */
        .long   0x00700000
        movea.l %d1,%a1                 /* 0xE24122  22 41             */
        move.w  MMU_$VA_SHIFT,%d0       /* 0xE24124  30 3a fc 0e in the image */
        lsl.l   %d0,%d5                 /* 0xE24128  e1 ad             */
        move.w  (PROC1_$AS_ID).l,%d5    /* 0xE2412A  3a 39 00 e2 06 0a */
        ror.l   #7,%d5                  /* 0xE24130  ee 9d             */
        swap    %d5                     /* 0xE24132  48 45             */
        move.w  %sr,-(%sp)              /* 0xE24134  40 e7             */
        ori.w   #0x700,%sr              /* 0xE24136  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d1       /* 0xE2413A  32 3a fb f0 in the image */
        bset    #1,%d1                  /* 0xE2413E  08 c1 00 01       */
        move.w  %d1,MMU_CSR             /* 0xE24142  33 c1 00 ff b4 00 */
        move.w  (%a1),%d0               /* 0xE24148  30 11             */
        .short  0xc0bc                  /* 0xE2414A  and.l #0xfff,%d0  */
        .long   0x00000fff
        beq.s   8f                      /* 0xE24150  67 32             */
        lsl.w   #2,%d0                  /* 0xE24152  e5 48             */
        move.w  %d0,%d4                 /* 0xE24154  38 00             */
        lea     PFT_BASE,%a0            /* 0xE24156  41 f9 00 ff b8 00 */
1:      move.l  (0,%a0,%d0.w),%d3       /* 0xE2415C  26 30 00 00       */
        move.l  %d3,%d1                 /* 0xE24160  22 03             */
        swap    %d1                     /* 0xE24162  48 41             */
        eor.w   %d5,%d1                 /* 0xE24164  bb 41             */
        .short  0xc27c, 0xfe0f          /* 0xE24166  and.w #0xfe0f,%d1 */
        beq.s   9f                      /* 0xE2416A  67 34             */
        .short  0xc27c, 0x000f          /* 0xE2416C  and.w #0xf,%d1    */
        bne.s   2f                      /* 0xE24170  66 06             */
        btst    #12,%d3                 /* 0xE24172  08 03 00 0c       */
        bne.s   9f                      /* 0xE24176  66 28             */
2:      move.w  %d3,%d0                 /* 0xE24178  30 03             */
        .short  0xc07c, 0x0fff          /* 0xE2417A  and.w #0xfff,%d0  */
        lsl.w   #2,%d0                  /* 0xE2417E  e5 48             */
        cmp.w   %d4,%d0                 /* 0xE24180  b0 44             */
        bne.s   1b                      /* 0xE24182  66 d8             */
8:      moveq   #0,%d0                  /* 0xE24184  70 00             */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE24186  33 fa fb a4 00 ff b4 00 in the image */
        move.w  (%sp)+,%sr              /* 0xE2418E  46 df             */
        movea.l (0x14,%sp),%a0          /* 0xE24190  20 6f 00 14       */
        move.l  #0x00070001,(%a0)       /* 0xE24194  20 bc 00 07 00 01 */
        movem.l (%sp)+,%d3-%d5          /* 0xE2419A  4c df 00 38       */
        rts                             /* 0xE2419E  4e 75             */
9:      lsr.w   #2,%d0                  /* 0xE241A0  e4 48             */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE241A2  33 fa fb 88 00 ff b4 00 in the image */
        move.w  (%sp)+,%sr              /* 0xE241AA  46 df             */
        movea.l (0x14,%sp),%a0          /* 0xE241AC  20 6f 00 14       */
        clr.l   (%a0)                   /* 0xE241B0  42 90             */
        movem.l (%sp)+,%d3-%d5          /* 0xE241B2  4c df 00 38       */
        rts                             /* 0xE241B6  4e 75             */

        .size   MMU_$VTOP, .-MMU_$VTOP
