/*
 * MMU_$POWER_OFF - Is bit 2 of the FPU-owner / power word set?
 *
 * Original address: 0x00E2428C, 18 bytes (0xE2428C..0xE2429D).
 *
 * Image bytes (`gsk read 0xe2428c 18`):
 *   00e2428c  30 39 00 ff b4 02 0a 40  00 00 08 00 00 02 56 c0
 *   00e2429c  4e 75
 *
 * The WORD at 0xFFB402 is read, `eori.w #0' (a no-op left in the image),
 * bit 2 tested, result the Domain boolean in %d0.b.  Byte-identical.
 */

        .section ".text.MMU_$POWER_OFF","ax",@progbits
        .even

        .equ    MMU_POWER_REG,  0x00FFB402  /* SAU2 MMU power register (hardware, SAU2_MMU_POWER_REG in arch/m68k/sau2/hw.h) */

        .globl  MMU_$POWER_OFF
        .globl  _MMU_$POWER_OFF

MMU_$POWER_OFF:
_MMU_$POWER_OFF:
        move.w  MMU_POWER_REG,%d0       /* 0xE2428C  30 39 00 ff b4 02 */
        eori.w  #0,%d0                  /* 0xE24292  0a 40 00 00       */
        btst    #2,%d0                  /* 0xE24296  08 00 00 02       */
        sne     %d0                     /* 0xE2429A  56 c0             */
        rts                             /* 0xE2429C  4e 75             */

        .size   MMU_$POWER_OFF, .-MMU_$POWER_OFF
