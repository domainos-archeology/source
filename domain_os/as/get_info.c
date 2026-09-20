/*
 * AS_$GET_INFO - Copy the address-space info record to the caller
 * Original address: 0x00e583a0 (80 bytes)
 *
 * Re-emitted from the disassembly.  Reached through the SVC table
 * (0x00E7B76A).  AS_$INFO is 0xE2B914 and AS_$INFO_SIZE the word at
 * 0xE2B970.
 *
 * Frame: (0x8,A6) buffer, (0xC,A6) req_size (pointer to a word),
 * (0x10,A6) actual_size (pointer to a word).
 *
 * 0x00E583A0  link.w A6,-0xc / pea (A2)
 * 0x00E583A6  A1 = req_size; A0 = actual_size
 * 0x00E583AE  tst.w (A1) / bgt 0x00E583B6
 * 0x00E583B2  *actual_size = 0; bra exit                 req <= 0: nothing
 * 0x00E583B6  D0 = *req_size; cmp.w AS_$INFO_SIZE,D0 / bgt
 * 0x00E583C0  *actual_size = D0; bra 0x00E583CA          req <= size: req
 * 0x00E583C4  *actual_size = AS_$INFO_SIZE               else the whole record
 * 0x00E583CA  A1 = &AS_$INFO; A2 = buffer
 * 0x00E583D4  D0 = *actual_size - 1; bmi exit
 * 0x00E583DA  D1 = 1; copy byte (-0x1,A1,D1) -> (-0x1,A2,D1); D1++; dbf D0
 *             (Pascal 1-based index, hence the -1 displacements)
 * 0x00E583E8  movea.l (-0x10,A6),A2 / unlk / rts
 *
 * All three compares are signed (`bgt', `bmi').
 */

#include "as/as_internal.h"

void AS_$GET_INFO(void *buffer, int16_t *req_size, int16_t *actual_size)
{
    const uint8_t *src = (const uint8_t *)&AS_$INFO;   /* A1 */
    uint8_t *dst = (uint8_t *)buffer;                   /* A2 */
    int16_t d0;
    uint16_t d1;

    /* 0x00E583AE: tst.w (A1) / bgt */
    if (*req_size <= 0) {
        /* 0x00E583B2 */
        *actual_size = 0;
        return;
    }

    /* 0x00E583B6..0x00E583C4 */
    if (*req_size <= AS_$INFO_SIZE) {
        *actual_size = *req_size;
    } else {
        *actual_size = AS_$INFO_SIZE;
    }

    /* 0x00E583D4..0x00E583E4: subq.w #1 / bmi / moveq #1 / dbf */
    d0 = (int16_t)(*actual_size - 1);
    if (d0 < 0) {
        return;
    }
    d1 = 1;
    for (; d0 >= 0; d0--) {
        dst[d1 - 1] = src[d1 - 1];
        d1++;
    }
}
