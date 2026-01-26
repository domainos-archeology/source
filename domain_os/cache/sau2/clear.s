/*
 * CACHE_$CLEAR - Clear/invalidate CPU instruction cache
 *
 * Clears the CPU instruction cache by reading the CACR (Cache Control
 * Register), setting bit 3 (CI - Clear Instruction cache), and writing
 * it back. This causes the 68020/030 to invalidate all instruction cache
 * entries.
 *
 * On the 68010 (SAU2), there is no instruction cache, but this code
 * exists for forward compatibility. The movec instruction to CACR will
 * be ignored or cause an exception on 68010, so this may need conditional
 * assembly or runtime detection for true 68010 support.
 *
 * CACR bit assignments (68020/030):
 *   Bit 0: Enable Instruction Cache
 *   Bit 1: Freeze Instruction Cache
 *   Bit 2: Clear Entry in Instruction Cache
 *   Bit 3: Clear Instruction Cache (CI) - clears entire I-cache
 *   Bits 4-7: Reserved (68020) / Data cache control (68030)
 *
 * Original address: 0x00E242D4
 * Size: 14 bytes
 *
 * Assembly:
 *   00e242d4    movec CACR,D0       ; Read Cache Control Register
 *   00e242d8    bset.l #0x3,D0      ; Set bit 3 (Clear I-cache)
 *   00e242dc    movec D0,CACR       ; Write back to CACR
 *   00e242e0    rts                 ; Return (D0 = new CACR value)
 */

    .text
    .globl  CACHE_$CLEAR
    .type   CACHE_$CLEAR, @function

CACHE_$CLEAR:
    /*
     * SAU2 (68010) has no instruction cache - this is a no-op.
     * On 68020/030, this would read CACR, set bit 3 (CI), and write back.
     * D0 = 0 to indicate no cache operation performed.
     *
     * TODO: For 68020+ SAU types, implement actual cache clear:
     *   movec %cacr, %d0
     *   bset  #3, %d0
     *   movec %d0, %cacr
     */
    moveq   #0, %d0
    rts

    .size   CACHE_$CLEAR, .-CACHE_$CLEAR
