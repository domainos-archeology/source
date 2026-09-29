/*
 * MMU_$MCR_CHANGE - Toggle one memory-control bit, and the MCR_SHADOW cell
 *
 * Original address: 0x00E242A0, 50 bytes of code (0xE242A0..0xE242D1)
 * followed by the two-byte MCR_SHADOW cell at 0xE242D2 (f0 00) that the
 * routine reaches with `lea (0x30,PC),%a0'; CACHE_$CLEAR starts at
 * 0xE242D4.
 *
 * Image bytes (`gsk read 0xe242a0 52`):
 *   00e242a0  41 fa 00 30 10 3a fa 88  67 0e 70 0b 90 6f 00 04
 *   00e242b0  01 79 00 ff b4 08 4e 75  30 2f 00 04 01 50 10 39
 *   00e242c0  00 ff b4 07 c0 3c 00 01  80 10 13 c0 00 ff b4 05
 *   00e242d0  4e 75 f0 00
 *
 * Argument: (4,SP) bit number, word.
 *   68020 (HIGH byte of M68020): bchg.b (0xb - bit) mod 8 in 0xFFB408
 *   68010: bchg.b bit mod 8 in MCR_SHADOW, then 0xFFB405 :=
 *          (0xFFB407 & 1) | MCR_SHADOW
 *
 * Deviation from the image bytes, forced by separate assembly:
 *   0xE242A4  move.b (-0x578,PC),%d0  -> move.b M68020,%d0          (+2)
 *   so the `lea (MCR_SHADOW,PC)' displacement assembles as 0x32.
 * `and.b #1,%d0' is the immediate-SOURCE form (c03c), spelled as .short.
 */

        .section ".text.MMU_$MCR_CHANGE","ax",@progbits
        .even

        .equ    M68020,         0x00E23D2E  /* MMU_ASM data cell (map 0xE23D2E), not yet an object: TODO(source-o56c) */
        .equ    MCR_020,        0x00FFB408  /* SAU2 MCR (hardware) */
        .equ    MCR_010,        0x00FFB405  /* SAU2 MCR (hardware) */
        .equ    MCR_MASK,       0x00FFB407  /* SAU2 MCR mask (hardware) */

        .globl  MMU_$MCR_CHANGE
        .globl  _MMU_$MCR_CHANGE
        .globl  MCR_SHADOW

MMU_$MCR_CHANGE:
_MMU_$MCR_CHANGE:
        lea     (MCR_SHADOW:w,%pc),%a0  /* 0xE242A0  41 fa 00 30 in the image */
        move.b  M68020,%d0              /* 0xE242A4  10 3a fa 88 in the image */
        beq.s   1f                      /* 0xE242A8  67 0e             */
        moveq   #11,%d0                 /* 0xE242AA  70 0b             */
        sub.w   (4,%sp),%d0             /* 0xE242AC  90 6f 00 04       */
        bchg    %d0,MCR_020             /* 0xE242B0  01 79 00 ff b4 08 */
        rts                             /* 0xE242B6  4e 75             */
1:      move.w  (4,%sp),%d0             /* 0xE242B8  30 2f 00 04       */
        bchg    %d0,(%a0)               /* 0xE242BC  01 50             */
        move.b  MCR_MASK,%d0            /* 0xE242BE  10 39 00 ff b4 07 */
        .short  0xc03c, 0x0001          /* 0xE242C4  and.b #1,%d0      */
        or.b    (%a0),%d0               /* 0xE242C8  80 10             */
        move.b  %d0,MCR_010             /* 0xE242CA  13 c0 00 ff b4 05 */
        rts                             /* 0xE242D0  4e 75             */
MCR_SHADOW:
        .byte   0xf0, 0x00              /* 0xE242D2  the 68010 MCR shadow */

        .size   MMU_$MCR_CHANGE, .-MMU_$MCR_CHANGE
