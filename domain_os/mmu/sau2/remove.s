/*
 * MMU_$REMOVE - Unlink one physical page from the reverse map
 *
 * Original address: 0x00E23D64, 46 bytes (0xE23D64..0xE23D91).
 *
 * Image bytes (`gsk read 0xe23d64 46`):
 *   00e23d64  48 e7 30 30 24 2f 00 14  40 e7 00 7c 07 00 30 3a
 *   00e23d74  ff b8 08 c0 00 01 33 c0  00 ff b4 00 61 4a 33 fa
 *   00e23d84  ff a8 00 ff b4 00 46 df  4c df 0c 0c 4e 75
 *
 * Argument: (0x14,SP) ppn, longword -> %d2 (after the 16-byte movem save).
 * SR saved on the stack, IPL 7, CSR := MMU_$PID_PRIV | 2, then
 * mmu_$remove_internal with %d2, CSR := MMU_$PID_PRIV, SR restored.
 *
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE23D72  move.w (-0x48,PC),%d0   -> move.w MMU_$PID_PRIV,%d0   (+2)
 *   0xE23D80  bsr.b mmu_$remove_internal -> jsr                    (+4)
 *   0xE23D82  move.w (-0x58,PC),CSR   -> move.w MMU_$PID_PRIV,CSR   (+2)
 */

        .section ".text.MMU_$REMOVE","ax",@progbits
        .even

        .equ    MMU_$PID_PRIV,  0x00E23D2C
        .equ    MMU_CSR,        0x00FFB400

        .globl  MMU_$REMOVE
        .globl  _MMU_$REMOVE
        .extern mmu_$remove_internal

MMU_$REMOVE:
_MMU_$REMOVE:
        movem.l %d2-%d3/%a2-%a3,-(%sp)  /* 0xE23D64  48 e7 30 30       */
        move.l  (0x14,%sp),%d2          /* 0xE23D68  24 2f 00 14       */
        move.w  %sr,-(%sp)              /* 0xE23D6C  40 e7             */
        ori.w   #0x700,%sr              /* 0xE23D6E  00 7c 07 00       */
        move.w  MMU_$PID_PRIV,%d0       /* 0xE23D72  30 3a ff b8 in the image */
        bset    #1,%d0                  /* 0xE23D76  08 c0 00 01       */
        move.w  %d0,MMU_CSR             /* 0xE23D7A  33 c0 00 ff b4 00 */
        jsr     mmu_$remove_internal    /* 0xE23D80  61 4a (bsr.b) in the image */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE23D82  33 fa ff a8 00 ff b4 00 in the image */
        move.w  (%sp)+,%sr              /* 0xE23D8A  46 df             */
        movem.l (%sp)+,%d2-%d3/%a2-%a3  /* 0xE23D8C  4c df 0c 0c       */
        rts                             /* 0xE23D90  4e 75             */

        .size   MMU_$REMOVE, .-MMU_$REMOVE
