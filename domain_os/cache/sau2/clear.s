/*
 * CACHE_$CLEAR - Clear/invalidate the CPU instruction cache
 *
 * Reads the CACR (Cache Control Register), sets bit 3 (CI - Clear
 * Instruction cache) and writes it back, invalidating every instruction
 * cache entry.  D0 is left holding the value written to CACR; no caller in
 * the image looks at it.
 *
 * CACR bit assignments (68020):
 *   Bit 0: Enable Instruction Cache
 *   Bit 1: Freeze Instruction Cache
 *   Bit 2: Clear Entry in Instruction Cache
 *   Bit 3: Clear Instruction Cache (CI) - clears the entire I-cache
 *
 * SAU2 is a 68020 board (the build defines CPU_M68020), so the CACR access
 * is the real behaviour and is transcribed as-is rather than stubbed.
 *
 * Original address: 0x00E242D4
 * Size: 14 bytes
 *
 * Image bytes (`gsk read 0xe242d4 14`):
 *   00e242d4  4e 7a 00 02 08 c0 00 03  4e 7b 00 02 4e 75
 *
 * Assembly:
 *   00e242d4    movec CACR,D0       ; Read Cache Control Register
 *   00e242d8    bset.l #0x3,D0      ; Set bit 3 (Clear I-cache)
 *   00e242dc    movec D0,CACR       ; Write back to CACR
 *   00e242e0    rts
 */

        .section ".text.CACHE_$CLEAR","ax",@progbits
    .globl  CACHE_$CLEAR
    .globl  _CACHE_$CLEAR
    .type   CACHE_$CLEAR, @function

CACHE_$CLEAR:
_CACHE_$CLEAR:
    movec   %cacr, %d0              /* 0x00E242D4 */
    bset    #3, %d0                 /* 0x00E242D8 */
    movec   %d0, %cacr              /* 0x00E242DC */
    rts                             /* 0x00E242E0 */

    .size   CACHE_$CLEAR, .-CACHE_$CLEAR
