/*
 * win/get_stats.c - WIN_$GET_STATS (0x00E19D62, 84 bytes)
 *
 * Jump-table entry +0x18, called by DISK_$GET_STATS as
 * get_stats(cnum, unit, stats).  Copies the 22-byte counter block at
 * WIN_$CNT (module +0x40) to `stats` for controller 0 / unit 0, and 22
 * zero bytes for any other pair.
 *
 * Frame (link.w A6,-0x18; A5 A2 D2 saved), A5 = 0xE2B89C:
 *   A6-0x18 22  zeros       `clr.l (-0x18,A6)`, four `clr.l (A1)+` and a
 *                           `clr.w (A1)+` from A6-0x14 (0x00E19D8A-0x00E19D9A)
 *   A1          source, A2 destination
 *   D2          dbf counter: 5 longwords, then one word
 */

#include "win/win_internal.h"

void WIN_$GET_STATS(int16_t cnum, int16_t unit, void *stats)
{
    uint32_t zeros[6];                  /* A6-0x18, 22 bytes used */
    const uint32_t *src;                /* A1 */
    uint32_t *dst = (uint32_t *)stats;  /* A2 */
    int16_t i;

    /* 0x00E19D7C-0x00E19D88 */
    if (cnum == 0 && unit == 0) {
        src = (const uint32_t *)(WIN_DATA_BASE + WIN_CNT_OFFSET);
    } else {
        /* 0x00E19D8A-0x00E19D9C */
        for (i = 0; i < 6; i++) {
            zeros[i] = 0;
        }
        src = zeros;
    }

    /* 0x00E19DA0-0x00E19DAA: `moveq #0x4,D2` / `dbf` = 5 longwords, then
     * one word. */
    for (i = 0; i < 5; i++) {
        *dst++ = *src++;
    }
    *(uint16_t *)dst = *(const uint16_t *)src;
}
