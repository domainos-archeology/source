/*
 * chksum/get_chksum.c - CHKSUM_$GET_CHKSUM, portable model
 *
 * Original address: 0x00E0A314 (20 bytes).
 *
 * On m68k this routine is hand-written assembly and is emitted verbatim in
 * chksum/sau2/get_chksum.s, so this file compiles to nothing there.  On
 * every other target it reproduces the same arithmetic.
 *
 * Full instruction trace:
 *   00e0a314  movea.l (0x4,SP),A0    ; A0 = va (no link; the arg is read
 *                                    ;      straight off the caller's stack)
 *   00e0a318  moveq #0x0,D0          ; running sum
 *   00e0a31a  move.w #0xff,D1w       ; dbf counter -> 256 iterations
 *   00e0a31e  add.w (A0)+,D0w        ; word 2i
 *   00e0a320  add.w (A0)+,D0w        ; word 2i+1
 *   00e0a322  dbf D1w,0x00e0a31e
 *   00e0a326  rts                    ; result in D0, Pascal function style
 *
 * Two properties matter for fidelity:
 *
 *   - The additions are `add.w`, and D0's high half is never read back, so
 *     carries out of bit 15 are discarded: the result is the sum of the
 *     words modulo 2^16.
 *   - The words are the m68k's own 16-bit words, i.e. BIG-endian byte
 *     pairs.  The loop below assembles each word from its two bytes so the
 *     same buffer produces the same checksum on a little-endian host.
 */

#include "chksum/chksum_internal.h"

#if !defined(ARCH_M68K)

uint16_t CHKSUM_$GET_CHKSUM(const void *va)
{
    const uint8_t *p = (const uint8_t *)va;   /* A0 */
    uint16_t sum = 0;                         /* D0.w */
    int16_t i;                                /* D1.w */

    /*
     * 0x00E0A31A: `move.w #0xff,D1w` then `dbf`, so the body runs
     * CHKSUM_LOOP_COUNT + 1 = 256 times, two words each.
     */
    for (i = CHKSUM_LOOP_COUNT; i >= 0; i--) {
        /* 0x00E0A31E */
        sum = (uint16_t)(sum + (uint16_t)(((uint16_t)p[0] << 8) | p[1]));
        p += 2;
        /* 0x00E0A320 */
        sum = (uint16_t)(sum + (uint16_t)(((uint16_t)p[0] << 8) | p[1]));
        p += 2;
    }

    /* 0x00E0A326: the sum comes back in D0. */
    return sum;
}

#endif /* !ARCH_M68K */
