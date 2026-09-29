/*
 * MMU_$INSTALL_PRIVATE - Install one page without the GLOBAL bit
 *
 * Original address: 0x00E23F82, 92 bytes (0xE23F82..0xE23FDD).
 *
 * Image bytes (`gsk read 0xe23f82 92`):
 *   00e23f82  48 e7 3f 38 24 2f 00 28  28 6f 00 2c 28 0c 32 3a
 *   00e23f92  fd a4 e3 ac 18 2f 00 33  ea 9c 18 2f 00 31 ee 9c
 *   00e23fa2  4a 79 00 e2 3d 2e 66 02  e4 4c c8 7c ff f0 40 c6
 *   00e23fb2  00 7c 07 00 30 3a fd 74  08 c0 00 01 33 c0 00 ff
 *   00e23fc2  b4 00 61 00 00 d6 02 6b  ef ff 00 02 33 fa fd 5c
 *   00e23fd2  00 ff b4 00 46 c6 4c df  1c fc 4e 75
 *
 * Arguments (after the 0x24-byte movem save):
 *   (0x28,SP)  ppn    longword -> %d2
 *   (0x2C,SP)  va     longword -> %a4
 *   (0x30,SP)  flags  longword: (0x31,SP) = asid, (0x33,SP) = prot
 *
 * Identical to MMU_$INSTALL up to the `bsr', then `andi.w #0xefff,(2,%a3)'
 * clears bit 12 (GLOBAL) of the PFT entry mmu_$installi left in %a3.
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE23F90  move.w (-0x25c,PC),%d1  -> move.w MMU_$PTT_SHIFT,%d1  (+2)
 *   0xE23FB6  move.w (-0x28c,PC),%d0  -> move.w MMU_$PID_PRIV,%d0   (+2)
 *   0xE23FC4  bsr.w mmu_$installi     -> jsr mmu_$installi          (+2)
 *   0xE23FCE  move.w (-0x2a4,PC),CSR  -> move.w MMU_$PID_PRIV,CSR   (+2)
 */

        .section ".text.MMU_$INSTALL_PRIVATE","ax",@progbits
        .even

        .equ    MMU_$PID_PRIV,  0x00E23D2C
        .equ    M68020,         0x00E23D2E
        .equ    MMU_$PTT_SHIFT, 0x00E23D36
        .equ    MMU_CSR,        0x00FFB400

        .globl  MMU_$INSTALL_PRIVATE
        .globl  _MMU_$INSTALL_PRIVATE
        .extern mmu_$installi

MMU_$INSTALL_PRIVATE:
_MMU_$INSTALL_PRIVATE:
        movem.l %d2-%d7/%a2-%a4,-(%sp)  /* 0xE23F82  48 e7 3f 38       */
        move.l  (0x28,%sp),%d2          /* 0xE23F86  24 2f 00 28       */
        movea.l (0x2c,%sp),%a4          /* 0xE23F8A  28 6f 00 2c       */
        move.l  %a4,%d4                 /* 0xE23F8E  28 0c             */
        move.w  MMU_$PTT_SHIFT,%d1      /* 0xE23F90  32 3a fd a4 in the image */
        lsl.l   %d1,%d4                 /* 0xE23F94  e3 ac             */
        move.b  (0x33,%sp),%d4          /* 0xE23F96  18 2f 00 33       */
        ror.l   #5,%d4                  /* 0xE23F9A  ea 9c             */
        move.b  (0x31,%sp),%d4          /* 0xE23F9C  18 2f 00 31       */
        ror.l   #7,%d4                  /* 0xE23FA0  ee 9c             */
        tst.w   M68020                  /* 0xE23FA2  4a 79 00 e2 3d 2e */
        bne.s   1f                      /* 0xE23FA8  66 02             */
        lsr.w   #2,%d4                  /* 0xE23FAA  e4 4c             */
1:      .short  0xc87c, 0xfff0          /* 0xE23FAC  and.w #0xfff0,%d4 */
        move.w  %sr,%d6                 /* 0xE23FB0  40 c6             */
        ori.w   #0x700,%sr              /* 0xE23FB2  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d0       /* 0xE23FB6  30 3a fd 74 in the image */
        bset    #1,%d0                  /* 0xE23FBA  08 c0 00 01       */
        move.w  %d0,MMU_CSR             /* 0xE23FBE  33 c0 00 ff b4 00 */
        jsr     mmu_$installi           /* 0xE23FC4  61 00 00 d6 (bsr.w) in the image */
        andi.w  #0xefff,(2,%a3)         /* 0xE23FC8  02 6b ef ff 00 02 */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE23FCE  33 fa fd 5c 00 ff b4 00 in the image */
        move.w  %d6,%sr                 /* 0xE23FD6  46 c6             */
        movem.l (%sp)+,%d2-%d7/%a2-%a4  /* 0xE23FD8  4c df 1c fc       */
        rts                             /* 0xE23FDC  4e 75             */

        .size   MMU_$INSTALL_PRIVATE, .-MMU_$INSTALL_PRIVATE
