/*
 * MMU_$INSTALL_LIST - Install a run of pages at consecutive virtual addresses
 *
 * Original address: 0x00E23FDE, 106 bytes (0xE23FDE..0xE24047).
 *
 * Image bytes (`gsk read 0xe23fde 106`):
 *   00e23fde  48 e7 3f 3c 2a 6f 00 2e  28 6f 00 32 3e 2f 00 2c
 *   00e23fee  53 47 2a 0c 32 3a fd 42  e3 ad 1a 2f 00 39 ea 9d
 *   00e23ffe  1a 2f 00 37 ee 9d 4a 79  00 e2 3d 2e 66 02 e4 4d
 *   00e2400e  ca 7c ff f0 40 c6 00 7c  07 00 30 3a fd 12 08 c0
 *   00e2401e  00 01 33 c0 00 ff b4 00  24 1d 28 05 61 70 da 7c
 *   00e2402e  00 10 49 ec 04 00 51 cf  ff f0 33 fa fc f2 00 ff
 *   00e2403e  b4 00 46 c6 4c df 3c fc  4e 75
 *
 * Arguments (after the 0x28-byte movem save):
 *   (0x2C,SP)  count      word -> %d7 (minus one, for `dbf': a zero count
 *                         installs 65536 pages)
 *   (0x2E,SP)  ppn_array  -> %a5, one longword per page, low word used
 *   (0x32,SP)  va         -> %a4
 *   (0x36,SP)  flags      longword: (0x37,SP) = asid, (0x39,SP) = prot
 *
 * %d5 is packed exactly as in MMU_$INSTALL (va << MMU_$PTT_SHIFT, prot,
 * ror 5, asid, ror 7, 68010 low-word shift, low nibble cleared); then
 * under IPL 7 with PTT access on, for each page mmu_$installi(%d2 = ppn,
 * %a4 = va, %d4 = %d5), `add.w #0x10' to %d5 (a WORD add) and 0x400 to
 * %a4.  The CSR is restored by re-reading MMU_$PID_PRIV.
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE23FF2  move.w (-0x2be,PC),%d1  -> move.w MMU_$PTT_SHIFT,%d1  (+2)
 *   0xE24018  move.w (-0x2ee,PC),%d0  -> move.w MMU_$PID_PRIV,%d0   (+2)
 *   0xE2402A  bsr.b mmu_$installi     -> jsr mmu_$installi          (+4)
 *   0xE24038  move.w (-0x30e,PC),CSR  -> move.w MMU_$PID_PRIV,CSR   (+2)
 */

        .section ".text.MMU_$INSTALL_LIST","ax",@progbits
        .even

        .equ    MMU_$PID_PRIV,  0x00E23D2C
        .equ    M68020,         0x00E23D2E
        .equ    MMU_$PTT_SHIFT, 0x00E23D36
        .equ    MMU_CSR,        0x00FFB400

        .globl  MMU_$INSTALL_LIST
        .globl  _MMU_$INSTALL_LIST
        .extern mmu_$installi

MMU_$INSTALL_LIST:
_MMU_$INSTALL_LIST:
        movem.l %d2-%d7/%a2-%a5,-(%sp)  /* 0xE23FDE  48 e7 3f 3c       */
        movea.l (0x2e,%sp),%a5          /* 0xE23FE2  2a 6f 00 2e       */
        movea.l (0x32,%sp),%a4          /* 0xE23FE6  28 6f 00 32       */
        move.w  (0x2c,%sp),%d7          /* 0xE23FEA  3e 2f 00 2c       */
        subq.w  #1,%d7                  /* 0xE23FEE  53 47             */
        move.l  %a4,%d5                 /* 0xE23FF0  2a 0c             */
        move.w  MMU_$PTT_SHIFT,%d1      /* 0xE23FF2  32 3a fd 42 in the image */
        lsl.l   %d1,%d5                 /* 0xE23FF6  e3 ad             */
        move.b  (0x39,%sp),%d5          /* 0xE23FF8  1a 2f 00 39       */
        ror.l   #5,%d5                  /* 0xE23FFC  ea 9d             */
        move.b  (0x37,%sp),%d5          /* 0xE23FFE  1a 2f 00 37       */
        ror.l   #7,%d5                  /* 0xE24002  ee 9d             */
        tst.w   M68020                  /* 0xE24004  4a 79 00 e2 3d 2e */
        bne.s   1f                      /* 0xE2400A  66 02             */
        lsr.w   #2,%d5                  /* 0xE2400C  e4 4d             */
1:      .short  0xca7c, 0xfff0          /* 0xE2400E  and.w #0xfff0,%d5 */
        move.w  %sr,%d6                 /* 0xE24012  40 c6             */
        ori.w   #0x700,%sr              /* 0xE24014  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d0       /* 0xE24018  30 3a fd 12 in the image */
        bset    #1,%d0                  /* 0xE2401C  08 c0 00 01       */
        move.w  %d0,MMU_CSR             /* 0xE24020  33 c0 00 ff b4 00 */
2:      move.l  (%a5)+,%d2              /* 0xE24026  24 1d             */
        move.l  %d5,%d4                 /* 0xE24028  28 05             */
        jsr     mmu_$installi           /* 0xE2402A  61 70 (bsr.b) in the image */
        .short  0xda7c, 0x0010          /* 0xE2402C  add.w #0x10,%d5   */
        lea     (0x400,%a4),%a4         /* 0xE24030  49 ec 04 00       */
        dbf     %d7,2b                  /* 0xE24034  51 cf ff f0       */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE24038  33 fa fc f2 00 ff b4 00 in the image */
        move.w  %d6,%sr                 /* 0xE24040  46 c6             */
        movem.l (%sp)+,%d2-%d7/%a2-%a5  /* 0xE24042  4c df 3c fc       */
        rts                             /* 0xE24046  4e 75             */

        .size   MMU_$INSTALL_LIST, .-MMU_$INSTALL_LIST
