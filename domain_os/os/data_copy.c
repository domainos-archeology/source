/*
 * OS_$DATA_COPY - copy a block of memory
 *
 * Original address: 0x00E11F04
 * Size: 62 bytes (0x00E11F04 .. 0x00E11F41)
 *
 * A hand-optimised copy without a link frame: the arguments are read
 * straight off the caller's stack.  When both addresses are even the bulk
 * is moved a longword at a time and the tail a byte at a time; otherwise
 * everything goes byte by byte.  Only the LOW WORD of the length is used,
 * and both loop counts are set up with `subq.w #1` / `blt`, so a low word
 * of 0x8001 or more copies nothing in the byte loop.
 *
 *   0x00E11F04  movem.l (0x4,SP),{A0 A1}    A0 = arg1, A1 = arg2
 *   0x00E11F0A  swap them                   A0 = dst (arg2), A1 = src (arg1)
 *   0x00E11F10  move.l (0xc,SP),D0          len
 *   0x00E11F14  btst.l #0,D1 (dst) / bne    odd dst -> byte loop
 *   0x00E11F1C  btst.l #0,D1 (src) / bne    odd src -> byte loop
 *   0x00E11F24  D1w = len; D0w = len & 3; D1w = (D1w >> 2) - 1; blt skip
 *   0x00E11F30  move.l (A1)+,(A0)+ / dbf D1w
 *   0x00E11F36  subq.w #1,D0w / blt done
 *   0x00E11F3A  move.b (A1)+,(A0)+ / dbf D0w
 *
 * Verified against the disassembly 2026-09-27; the body was already
 * faithful and is restated with the register widths made explicit.
 */

#include "os/os_internal.h"

void OS_$DATA_COPY(const void *src_v, void *dst_v, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)src_v;      /* A1 */
    uint8_t *dst = (uint8_t *)dst_v;                  /* A0 */
    uint16_t d0w = (uint16_t)len;                     /* D0w */
    int16_t d1w;                                      /* D1w */
    int16_t count;

    /* 0x00E11F14 .. 0x00E11F22: both addresses even? */
    if ((ARCH_PTR_TO_VA(dst) & 1) == 0 && (ARCH_PTR_TO_VA(src) & 1) == 0) {
        /* 0x00E11F24 .. 0x00E11F2E */
        d1w = (int16_t)(d0w >> 2);
        d0w &= 3;
        d1w = (int16_t)(d1w - 1);
        if (d1w >= 0) {
            /* 0x00E11F30 .. 0x00E11F32: d1w + 1 longwords */
            for (count = d1w; count != -1; count--) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
                src += 4;
                dst += 4;
            }
        }
    }

    /* 0x00E11F36 .. 0x00E11F3C: `subq.w #1` / `blt` - a signed test */
    d1w = (int16_t)(d0w - 1);
    if (d1w >= 0) {
        for (count = d1w; count != -1; count--) {
            *dst++ = *src++;
        }
    }
}
