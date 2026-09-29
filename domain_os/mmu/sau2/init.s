/*
 * MMU_$INIT - Pick the 68020 or 68010 MMU parameters at boot
 *
 * Original address: 0x00E23D38, 44 bytes (0xE23D38..0xE23D63).  Single
 * caller: OS_$INIT at 0x00E3383E.
 *
 * Image bytes (`gsk read 0xe23d38 44`):
 *   00e23d38  2f 0d 4b fa ff f2 4a 55  67 18 2b 7c 00 3f fc 00
 *   00e23d48  00 02 3b 7c 00 01 00 06  3b 7c 00 06 00 08 2a 5f
 *   00e23d58  4e 75 3b 7a ff fc 05 a6  2a 5f 4e 75
 *
 * Hand-written assembly: A5 is saved by hand and pointed with
 * `lea (-0xe,PC),A5' at the MMU_ASM data block, whose first word is the
 * M68020 boolean (0xE23D2E; `D E23D2C MMU_ASM' in the SAU2 map).  The
 * cells it writes, as (d,A5):
 *   (0x2,A5)   0xE23D30  VA_TO_PTT_OFFSET_MASK  = 0x3FFC00 on a 68020
 *   (0x6,A5)   0xE23D34  MMU_$VA_SHIFT          = 1
 *   (0x8,A5)   0xE23D36  MMU_$PTT_SHIFT         = 6
 *   (0x5a6,A5) 0xE242D4  CACHE_$CLEAR           <- the `rts' opcode
 * On a 68010 (M68020 == 0, tested as a WHOLE WORD with `tst.w (A5)') the
 * image-shipped defaults (0x0FFC00, 3, 8) are left alone and the first
 * word of CACHE_$CLEAR is overwritten with 0x4E75, copied from this very
 * routine's own `rts' at 0xE23D58 (`move.w (-0x4,PC),(0x5a6,A5)'), which
 * turns the cache-clear routine into a no-op.
 *
 * Deviation from the image bytes: `lea (-0xe,PC),A5' addresses a data cell
 * in another object; a 16-bit PC-relative displacement cannot survive
 * separate assembly, so it is `lea M68020,%a5' (6 bytes instead of 4).
 * Everything after it is byte-identical to the image; the `(-0x4,PC)'
 * read stays PC-relative because its target (local label 1) is in this
 * file.
 */

        .section ".text.MMU_$INIT","ax",@progbits
        .even

        .equ    M68020, 0x00E23D2E

        .globl  MMU_$INIT
        .globl  _MMU_$INIT

MMU_$INIT:
_MMU_$INIT:
        move.l  %a5,-(%sp)              /* 0xE23D38  2f 0d             */
        lea     M68020,%a5              /* 0xE23D3A  4b fa ff f2 in the image */
        tst.w   (%a5)                   /* 0xE23D3E  4a 55             */
        beq.s   2f                      /* 0xE23D40  67 18             */
        move.l  #0x3ffc00,(2,%a5)       /* 0xE23D42  2b 7c 00 3f fc 00 00 02 */
        move.w  #1,(6,%a5)              /* 0xE23D4A  3b 7c 00 01 00 06 */
        move.w  #6,(8,%a5)              /* 0xE23D50  3b 7c 00 06 00 08 */
        movea.l (%sp)+,%a5              /* 0xE23D56  2a 5f             */
1:      rts                             /* 0xE23D58  4e 75             */
2:      move.w  1b(%pc),(0x5a6,%a5)     /* 0xE23D5A  3b 7a ff fc 05 a6 */
        movea.l (%sp)+,%a5              /* 0xE23D60  2a 5f             */
        rts                             /* 0xE23D62  4e 75             */
