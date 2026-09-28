/*
 * MMU_$NORMAL_MODE - Is the MMU status register's "normal mode" bit set?
 *
 * Original address: 0x00E24280, 12 bytes (0xE24280..0xE2428B).
 *
 * Image bytes (`gsk read 0xe24280 12`):
 *   00e24280  08 39 00 04 00 ff b4 03  56 c0 4e 75
 *
 * `btst #4' of the byte at 0xFFB403 (bit 4 = normal mode, Engineering
 * Handbook p. 7-25), result the Domain boolean in %d0.b.  Byte-identical.
 */

        .text
        .even

        .equ    MMU_STATUS_REG, 0x00FFB403

        .globl  MMU_$NORMAL_MODE
        .globl  _MMU_$NORMAL_MODE

MMU_$NORMAL_MODE:
_MMU_$NORMAL_MODE:
        btst    #4,MMU_STATUS_REG       /* 0xE24280  08 39 00 04 00 ff b4 03 */
        sne     %d0                     /* 0xE24288  56 c0             */
        rts                             /* 0xE2428A  4e 75             */

        .size   MMU_$NORMAL_MODE, .-MMU_$NORMAL_MODE
