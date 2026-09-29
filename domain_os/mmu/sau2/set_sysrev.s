/*
 * MMU_$SYSTEM_REV cell and MMU_$SET_SYSREV
 *
 * Original addresses: MMU_$SYSTEM_REV 0x00E2426E (4 bytes, zero in the
 * image; the SAU2 map names it), MMU_$SET_SYSREV 0x00E24272, 14 bytes.
 *
 * Image bytes (`gsk read 0xe2426e 18`):
 *   00e2426e  00 00 00 00 41 fa ff fa  11 79 00 ff b4 09 00 03
 *   00e2427e  4e 75
 *
 * `lea (-0x6,PC),%a0' at 0xE24272 addresses 0xE2426E, and the hardware
 * revision byte (0xFFB409) is stored at (3,%a0) = 0xE24271: the LOW byte
 * of MMU_$SYSTEM_REV.  Both live in this object, so the routine is
 * byte-identical.
 */

        .section ".text.MMU_$SYSTEM_REV","ax",@progbits
        .even

        .equ    DN330_MMU_HARDWARE_REV, 0x00FFB409  /* SAU2 MMU revision register (hardware) */

        .globl  MMU_$SYSTEM_REV
        .globl  _MMU_$SYSTEM_REV
        .globl  MMU_$SET_SYSREV
        .globl  _MMU_$SET_SYSREV

MMU_$SYSTEM_REV:
_MMU_$SYSTEM_REV:
        .long   0                       /* 0xE2426E  00 00 00 00       */
MMU_$SET_SYSREV:
_MMU_$SET_SYSREV:
        lea     (MMU_$SYSTEM_REV:w,%pc),%a0 /* 0xE24272  41 fa ff fa   */
        move.b  DN330_MMU_HARDWARE_REV,(3,%a0) /* 0xE24276  11 79 00 ff b4 09 00 03 */
        rts                             /* 0xE2427E  4e 75             */

        .size   MMU_$SET_SYSREV, .-MMU_$SET_SYSREV
