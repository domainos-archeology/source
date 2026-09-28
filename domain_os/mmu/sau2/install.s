/*
 * MMU_$INSTALL - Install one virtual-to-physical translation
 *
 * Original address: 0x00E24048, 84 bytes (0xE24048..0xE2409B).  Twenty
 * callers (AST, MST, NETWORK, PMAP, the drivers).
 *
 * Image bytes (`gsk read 0xe24048 84`):
 *   00e24048  48 e7 3f 38 24 2f 00 28  28 6f 00 2c 28 0c 32 3a
 *   00e24058  fc de e3 ac 18 2f 00 33  ea 9c 18 2f 00 31 ee 9c
 *   00e24068  4a 79 00 e2 3d 2e 66 02  e4 4c c8 7c ff f0 40 c6
 *   00e24078  00 7c 07 00 30 3a fc ae  08 c0 00 01 33 c0 00 ff
 *   00e24088  b4 00 61 10 33 fa fc 9e  00 ff b4 00 46 c6 4c df
 *   00e24098  1c fc 4e 75
 *
 * Hand-written assembly: a `movem.l' of nine registers with no link frame,
 * arguments read at fixed SP offsets, an SR save/`ori #0x700'/restore
 * bracket around a CSR write, and a `bsr' to mmu_$installi (0xE2409C)
 * that takes its operands in registers.
 *
 * Arguments (after the 0x24-byte movem save, (0x28,SP) is the first):
 *   (0x28,SP)  ppn    longword          -> %d2
 *   (0x2C,SP)  va     longword          -> %a4
 *   (0x30,SP)  flags  longword: (0x31,SP) = its second byte, the ASID;
 *                                (0x33,SP) = its last byte, the protection
 *
 * 0xE24054-0xE24066  %d4 = va << MMU_$PTT_SHIFT (0xE23D36, read PC-relative
 *                    in the image), low byte := prot, ror.l #5, low byte :=
 *                    asid, ror.l #7 - the packed PFT word pair
 * 0xE24068-0xE24070  on a 68010 (`tst.w M68020' on the whole word) the low
 *                    word is shifted right two more bits
 * 0xE24072           `and.w #0xfff0': the low nibble is cleared
 * 0xE24076-0xE24084  SR saved in %d6, IPL 7, CSR (0xFFB400) := MMU_$PID_PRIV
 *                    (0xE23D2C) with bit 1 set - PTT access enabled
 * 0xE2408A           bsr mmu_$installi with %d2 = ppn, %a4 = va, %d4 = packed
 * 0xE2408C-0xE24094  CSR := MMU_$PID_PRIV; SR restored from %d6
 *
 * Deviations from the image bytes, all forced by separate assembly (the
 * three PC-relative data reads and the bsr target live in other objects):
 *   0xE24056  move.w (-0x322,PC),%d1  -> move.w MMU_$PTT_SHIFT,%d1  (+2)
 *   0xE2407C  move.w (-0x352,PC),%d0  -> move.w MMU_$PID_PRIV,%d0   (+2)
 *   0xE2408A  bsr.b mmu_$installi     -> jsr mmu_$installi          (+4)
 *   0xE2408C  move.w (-0x362,PC),CSR  -> move.w MMU_$PID_PRIV,CSR   (+2)
 * The remaining instructions are byte-identical.
 *
 * TODO(source-w78q): mmu_$installi is register-called (%d2/%a4/%d4); the
 * C rendition in mmu/internal.c takes stack arguments and belongs to the
 * mmu/internal.c re-emission.
 */

        .text
        .even

        .equ    MMU_$PID_PRIV,  0x00E23D2C
        .equ    M68020,         0x00E23D2E
        .equ    MMU_$PTT_SHIFT, 0x00E23D36
        .equ    MMU_CSR,        0x00FFB400

        .globl  MMU_$INSTALL
        .globl  _MMU_$INSTALL
        .extern mmu_$installi

MMU_$INSTALL:
_MMU_$INSTALL:
        movem.l %d2-%d7/%a2-%a4,-(%sp)  /* 0xE24048  48 e7 3f 38       */
        move.l  (0x28,%sp),%d2          /* 0xE2404C  24 2f 00 28       */
        movea.l (0x2c,%sp),%a4          /* 0xE24050  28 6f 00 2c       */
        move.l  %a4,%d4                 /* 0xE24054  28 0c             */
        move.w  MMU_$PTT_SHIFT,%d1      /* 0xE24056  32 3a fc de in the image */
        lsl.l   %d1,%d4                 /* 0xE2405A  e3 ac             */
        move.b  (0x33,%sp),%d4          /* 0xE2405C  18 2f 00 33       */
        ror.l   #5,%d4                  /* 0xE24060  ea 9c             */
        move.b  (0x31,%sp),%d4          /* 0xE24062  18 2f 00 31       */
        ror.l   #7,%d4                  /* 0xE24066  ee 9c             */
        tst.w   M68020                  /* 0xE24068  4a 79 00 e2 3d 2e */
        bne.s   1f                      /* 0xE2406E  66 02             */
        lsr.w   #2,%d4                  /* 0xE24070  e4 4c             */
1:      .short  0xc87c, 0xfff0          /* 0xE24072  and.w #0xfff0,%d4 (immediate-source form, gas would emit andi 0244) */
        move.w  %sr,%d6                 /* 0xE24076  40 c6             */
        ori.w   #0x700,%sr              /* 0xE24078  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d0       /* 0xE2407C  30 3a fc ae in the image */
        bset    #1,%d0                  /* 0xE24080  08 c0 00 01       */
        move.w  %d0,MMU_CSR             /* 0xE24084  33 c0 00 ff b4 00 */
        jsr     mmu_$installi           /* 0xE2408A  61 10 (bsr.b) in the image */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE2408C  33 fa fc 9e 00 ff b4 00 in the image */
        move.w  %d6,%sr                 /* 0xE24094  46 c6             */
        movem.l (%sp)+,%d2-%d7/%a2-%a4  /* 0xE24096  4c df 1c fc       */
        rts                             /* 0xE2409A  4e 75             */
