/*
 * PROC1_$LOADAV_CALLBACK - Load average calculation callback
 * Original address: 0x00e14bda
 *
 * Called periodically to update the load averages using exponential
 * weighted moving average (EWMA). Uses different decay constants
 * for 1, 5, and 15 minute averages.
 *
 * The formula is: new_avg = old_avg * decay + ready_count * scale
 *
 * The values are stored as 8.24 fixed-point numbers.
 */

#include "proc1/proc1_internal.h"
#include "math/math.h"

/*
 * EWMA decay constants (as 16-bit fixed point fractions):
 * 1-min:  0xeb88 ≈ 0.919 (exp(-5/60))
 * 5-min:  0xfbc5 ≈ 0.983 (exp(-5/300))
 * 15-min: 0xfe95 ≈ 0.994 (exp(-5/900))
 *
 * Scale factors to convert ready_count to load contribution:
 * 1-min:  0x1478 = 5240 (scaled by sample interval)
 * 5-min:  0x043b = 1083
 * 15-min: 0x016b = 363
 */
#define DECAY_1MIN      0xeb88
#define DECAY_5MIN      0xfbc5
#define DECAY_15MIN     0xfe95

#define SCALE_1MIN      0x1478
#define SCALE_5MIN      0x043b
#define SCALE_15MIN     0x016b

void PROC1_$LOADAV_CALLBACK(void)
{
    int32_t temp;
    int16_t ready_count;

    /* Get current ready count */
    ready_count = PROC1_$READY_COUNT;

    /*
     * Update 1-minute average:
     * new = (old >> 8) * DECAY_1MIN >> 8 + ready_count * SCALE_1MIN
     *
     * The arithmetic shift right with rounding for negative numbers:
     * if (val < 0) val = (val + 0xff) >> 8; else val = val >> 8;
     */
    temp = LOADAV_1MIN;
    if (temp < 0) temp += 0xff;
    temp >>= 8;
    temp = M$MIS$LLL(temp, DECAY_1MIN);
    if (temp < 0) temp += 0xff;
    temp >>= 8;
    LOADAV_1MIN = temp + ((int32_t)ready_count * SCALE_1MIN);

    /*
     * Update 5-minute average
     */
    temp = LOADAV_5MIN;
    if (temp < 0) temp += 0xff;
    temp >>= 8;
    temp = M$MIS$LLL(temp, DECAY_5MIN);
    if (temp < 0) temp += 0xff;
    temp >>= 8;
    LOADAV_5MIN = temp + ((int32_t)ready_count * SCALE_5MIN);

    /*
     * Update 15-minute average
     */
    temp = LOADAV_15MIN;
    if (temp < 0) temp += 0xff;
    temp >>= 8;
    temp = M$MIS$LLL(temp, DECAY_15MIN);
    if (temp < 0) temp += 0xff;
    temp >>= 8;
    LOADAV_15MIN = temp + ((int32_t)ready_count * SCALE_15MIN);
}
