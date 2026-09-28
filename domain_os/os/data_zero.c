/*
 * OS_$DATA_ZERO - zero a block of memory
 *
 * Original address: 0x00E11F42
 * Size: 88 bytes (0x00E11F42 .. 0x00E11F99)
 *
 * Frame (link.w A6,0x0):
 *   (0x8,A6)   ptr    -> A1
 *   (0xc,A6)   len    -> D0; zero means nothing to do
 *
 *   0x00E11F50  movea.l #0,A0                 the zero source for `move.l A0,(A1)+`
 *   0x00E11F56  btst.l #0,D1 (ptr) / beq      odd start: one byte, len--
 *   0x00E11F64  move.l D0,D1                  D1 = the (aligned) remaining length
 *   0x00E11F66  subq.l #4,D0 / bmi            fewer than 4 bytes: no longwords
 *   0x00E11F6A  lsr.l #2,D0                   D0 = longwords - 1
 *   0x00E11F6C  cmpi.l #0xffff,D0 / bls       more than 0x10000 longwords:
 *   0x00E11F74    move.l A0,(A1)+ / subq.l #1,D0 / bpl    a 32-bit counted loop
 *   0x00E11F7C    move.l A0,(A1)+ / dbf D0w               else a dbf loop
 *   0x00E11F82  btst.l #1,D1 / move.w #0,(A1)+  a trailing word
 *   0x00E11F8C  btst.l #0,D1 / move.b #0,(A1)+  a trailing byte
 *
 * Both longword loops run D0 + 1 = len / 4 times; the tail uses bits 1
 * and 0 of the aligned length.
 *
 * Verified against the disassembly 2026-09-27; the body was equivalent and
 * is restated in the image's shape.
 */

#include "os/os_internal.h"

void OS_$DATA_ZERO(void *ptr_v, uint32_t len)
{
    uint8_t *p = (uint8_t *)ptr_v;      /* A1 */
    uint32_t d0 = len;                  /* D0 */
    uint32_t d1;                        /* D1 */

    /* 0x00E11F46 .. 0x00E11F4A */
    if (d0 == 0) {
        return;
    }

    /* 0x00E11F56 .. 0x00E11F62: align to an even address */
    if ((ARCH_PTR_TO_VA(p) & 1) != 0) {
        *p++ = 0;
        d0--;
    }

    /* 0x00E11F64 .. 0x00E11F68 */
    d1 = d0;
    if ((int32_t)(d0 - 4) >= 0) {
        d0 = (d0 - 4) >> 2;                          /* 0x00E11F6A */
        if (d0 > 0xFFFF) {
            /* 0x00E11F74 .. 0x00E11F78: subq.l / bpl */
            do {
                p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0;
                p += 4;
                d0--;
            } while ((int32_t)d0 >= 0);
        } else {
            /* 0x00E11F7C .. 0x00E11F7E: dbf on the low word */
            uint16_t d0w = (uint16_t)d0;
            do {
                p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0;
                p += 4;
                d0w--;
            } while (d0w != 0xFFFF);
        }
    }

    /* 0x00E11F82 .. 0x00E11F88 */
    if ((d1 & 2) != 0) {
        p[0] = 0;
        p[1] = 0;
        p += 2;
    }

    /* 0x00E11F8C .. 0x00E11F92 */
    if ((d1 & 1) != 0) {
        *p = 0;
    }
}
