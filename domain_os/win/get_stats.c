/*
 * WIN_$GET_STATS - Get Winchester Statistics
 *
 * Returns statistics counters for the Winchester driver.
 * If both params are 0, returns real counters; otherwise
 * returns zeroed structure.
 *
 * @param param_1  First parameter (0 for real stats)
 * @param param_2  Second parameter (0 for real stats)
 * @param stats    Output: statistics structure (22 bytes)
 */

#include "win.h"

void WIN_$GET_STATS(int16_t param_1, int16_t param_2, void *stats)
{
    uint8_t *win_data = WIN_DATA_BASE;
    uint32_t *src;
    uint32_t *dst = (uint32_t *)stats;
    int16_t i;
    uint8_t local_stats[22];

    /* Check if real stats requested */
    if (param_1 == 0 && param_2 == 0) {
        src = (uint32_t *)(win_data + WIN_CNT_OFFSET);
    } else {
        /* Return zeroed stats */
        for (i = 0; i < 22; i++) {
            local_stats[i] = 0;
        }
        src = (uint32_t *)local_stats;
    }

    /* Copy 5 longs (20 bytes) */
    for (i = 0; i < 5; i++) {
        *dst++ = *src++;
    }

    /* Copy final word (2 bytes) */
    *(uint16_t *)dst = *(uint16_t *)src;
}
