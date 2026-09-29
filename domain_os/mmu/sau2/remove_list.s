/*
 * MMU_$REMOVE_LIST - Unlink an array of physical pages
 *
 * Original address: 0x00E23D92, 58 bytes (0xE23D92..0xE23DCB).
 *
 * Image bytes (`gsk read 0xe23d92 58`):
 *   00e23d92  48 e7 33 38 28 6f 00 20  3e 2f 00 24 53 47 40 c6
 *   00e23da2  00 7c 07 00 30 3a ff 84  08 c0 00 01 33 c0 00 ff
 *   00e23db2  b4 00 24 1c 61 14 51 cf  ff fa 33 fa ff 6e 00 ff
 *   00e23dc2  b4 00 46 c6 4c df 1c cc  4e 75
 *
 * Arguments (after the 28-byte movem save):
 *   (0x20,SP)  ppn_array  -> %a4, one longword per page
 *   (0x24,SP)  count      word -> %d7 minus one for `dbf' (a zero count
 *                         removes 65536 pages)
 * SR kept in %d6, IPL 7, CSR := MMU_$PID_PRIV | 2, mmu_$remove_internal
 * for each %d2 = (%a4)+, CSR := MMU_$PID_PRIV, SR restored.
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE23DA6  move.w (-0x7c,PC),%d0   -> move.w MMU_$PID_PRIV,%d0   (+2)
 *   0xE23DB6  bsr.b mmu_$remove_internal -> jsr                    (+4)
 *   0xE23DBC  move.w (-0x92,PC),CSR   -> move.w MMU_$PID_PRIV,CSR   (+2)
 */

        .section ".text.MMU_$REMOVE_LIST","ax",@progbits
        .even

        .equ    MMU_$PID_PRIV,  0x00E23D2C
        .equ    MMU_CSR,        0x00FFB400

        .globl  MMU_$REMOVE_LIST
        .globl  _MMU_$REMOVE_LIST
        .extern mmu_$remove_internal

MMU_$REMOVE_LIST:
_MMU_$REMOVE_LIST:
        movem.l %d2-%d3/%d6-%d7/%a2-%a4,-(%sp) /* 0xE23D92  48 e7 33 38 */
        movea.l (0x20,%sp),%a4          /* 0xE23D96  28 6f 00 20       */
        move.w  (0x24,%sp),%d7          /* 0xE23D9A  3e 2f 00 24       */
        subq.w  #1,%d7                  /* 0xE23D9E  53 47             */
        move.w  %sr,%d6                 /* 0xE23DA0  40 c6             */
        ori.w   #0x700,%sr              /* 0xE23DA2  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d0       /* 0xE23DA6  30 3a ff 84 in the image */
        bset    #1,%d0                  /* 0xE23DAA  08 c0 00 01       */
        move.w  %d0,MMU_CSR             /* 0xE23DAE  33 c0 00 ff b4 00 */
1:      move.l  (%a4)+,%d2              /* 0xE23DB4  24 1c             */
        jsr     mmu_$remove_internal    /* 0xE23DB6  61 14 (bsr.b) in the image */
        dbf     %d7,1b                  /* 0xE23DB8  51 cf ff fa       */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE23DBC  33 fa ff 6e 00 ff b4 00 in the image */
        move.w  %d6,%sr                 /* 0xE23DC4  46 c6             */
        movem.l (%sp)+,%d2-%d3/%d6-%d7/%a2-%a4 /* 0xE23DC6  4c df 1c cc */
        rts                             /* 0xE23DCA  4e 75             */

        .size   MMU_$REMOVE_LIST, .-MMU_$REMOVE_LIST
