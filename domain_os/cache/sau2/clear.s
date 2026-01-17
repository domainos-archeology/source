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
     * Read current CACR value into D0.
     * CACR is control register 0x002.
     */
    movec   %cacr, %d0

    /*
     * Set bit 3 (CI - Clear Instruction cache).
     * This tells the processor to invalidate all instruction cache entries.
     */
    bset    #3, %d0

    /*
     * Write the modified value back to CACR.
     * The CI bit is write-only and self-clearing; it triggers the
     * cache clear operation when written as 1.
     */
    movec   %d0, %cacr

    /*
     * Return with D0 containing the value written to CACR.
     * (The CI bit will read back as 0 since it's self-clearing.)
     */
    rts

    .size   CACHE_$CLEAR, .-CACHE_$CLEAR
