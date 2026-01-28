/*
 * PCHIST_$CNTL - Control system-wide PC histogram
 *
 * Reverse engineered from Domain/OS at address 0x00e5cdb6
 */

#include "pchist/pchist_internal.h"
#include "mst/mst.h"
#include "math/math.h"

/*
 * Alignment flag for command 3
 */
int8_t PCHIST_$DOALIGN;

/*
 * Copy histogram data to output buffer
 * Size is 0x10A * 4 bytes = 1064 bytes (plus 2 byte trailer)
 */
static void copy_histogram_data(void *dest)
{
    uint32_t *src = (uint32_t *)&PCHIST_$HISTOGRAM;
    uint32_t *dst = (uint32_t *)dest;
    int16_t count;

    /* Copy 0x10A (266) longwords */
    for (count = 0x109; count >= 0; count--) {
        *dst++ = *src++;
    }
    /* Copy final word */
    *(uint16_t *)dst = *(uint16_t *)src;
}

/*
 * PCHIST_$CNTL
 *
 * Control system-wide PC histogram profiling.
 *
 * Commands:
 *   0 - Start profiling with new parameters
 *   1 - Stop profiling and return current data
 *   2 - (unused, returns current data)
 *   3 - Start profiling with alignment mode
 */
void PCHIST_$CNTL(
    int16_t *cmd_ptr,
    uint32_t *range_ptr,
    void *data_ptr,
    status_$t *status_ret)
{
    int16_t cmd;
    uint32_t range_start, range_end;
    uint32_t range_size;
    uint32_t buckets;
    uint16_t shift;
    uint32_t multiplier;
    uint32_t bucket_size;
    int16_t i;
    int16_t pid_param;
    uid_t uid;

    *status_ret = status_$ok;
    cmd = *cmd_ptr;

    /*
     * Commands 0 and 3: Start profiling
     * Other commands: Stop/query
     */
    if (cmd != 0 && cmd != 3) {
        /* Command 1 or 2: Stop profiling if cmd == 1, then return data */
        if (cmd == 1) {
            PCHIST_$STOP_PROFILING();
        }
        /* Copy current histogram data to output */
        copy_histogram_data(data_ptr);
        *status_ret = status_$ok;
        return;
    }

    /*
     * Commands 0 and 3: Start new profiling session
     */

    /* First stop any existing profiling */
    PCHIST_$STOP_PROFILING();

    /*
     * Calculate profiling parameters from the range
     * range_ptr[0] = range start
     * range_ptr[1] = range end
     * range_ptr[2] = PID filter (negative = UPID to convert)
     */
    range_start = range_ptr[0];
    range_end = range_ptr[1];

    if (range_start == 0 && range_end == 0) {
        /* No range specified - use defaults */
        multiplier = 0x100;
        bucket_size = 0x1000000;
        shift = 0x18;
    }
    else {
        /* Calculate range size */
        if (range_end < range_start) {
            range_size = 0x200;  /* Default minimum */
        }
        else {
            range_size = range_end - range_start + 1;
        }

        if (range_size == 0) {
            /* Edge case: use defaults */
            multiplier = 0x100;
            bucket_size = 0x1000000;
            shift = 0x18;
        }
        else {
            /*
             * Calculate number of buckets needed
             * Round up to next power of 2, capped at 256
             */
            buckets = range_size >> 8;
            if ((range_size & 0xFF) != 0) {
                buckets++;
            }

            /* Find shift count (log2 of bucket size) */
            shift = 1;
            for (bucket_size = 2; bucket_size < buckets; bucket_size <<= 1) {
                shift++;
            }

            /* Calculate multiplier (entries per bucket) */
            multiplier = (bucket_size + range_size - 1) >> shift;
            if (multiplier == 0) {
                multiplier = 0x100;
            }
        }
    }

    /*
     * Clear histogram bins
     */
    for (i = 0; i < PCHIST_HISTOGRAM_BINS; i++) {
        PCHIST_$HISTOGRAM.histogram[i] = 0;
    }

    /*
     * Handle PID filter
     * Negative value means it's a UPID that needs to be converted
     */
    pid_param = (int16_t)range_ptr[2];
    if (pid_param < 0) {
        int16_t upid = -pid_param;
        PROC2_$UPID_TO_UID(&upid, &uid, status_ret);
        if (*status_ret != status_$ok) {
            PCHIST_$UNWIRE_CLEANUP();
            return;
        }
        PCHIST_$HISTOGRAM.pid_filter = PROC2_$GET_PID(&uid, status_ret);
        if (*status_ret != status_$ok) {
            PCHIST_$UNWIRE_CLEANUP();
            return;
        }
    }
    else {
        PCHIST_$HISTOGRAM.pid_filter = pid_param;
    }

    /*
     * Set up histogram parameters
     */
    PCHIST_$HISTOGRAM.range_start = range_start;
    PCHIST_$HISTOGRAM.range_end = range_start + M$MIU$LLW(bucket_size, multiplier) - 1;
    PCHIST_$HISTOGRAM.multiplier = (uint16_t)multiplier;
    PCHIST_$HISTOGRAM.bucket_size = bucket_size;
    PCHIST_$HISTOGRAM.shift = shift;
    PCHIST_$HISTOGRAM.total_samples = 0;
    PCHIST_$HISTOGRAM.over_range = 0;
    PCHIST_$HISTOGRAM.under_range = 0;
    PCHIST_$HISTOGRAM.wrong_pid = 0;
    PCHIST_$HISTOGRAM.doalign = -1;  /* 0xFF = true */
    PCHIST_$HISTOGRAM.enabled = 1;

    /*
     * Wire the histogram buffer pages
     * This ensures the buffer stays in memory during profiling
     */
    /* TODO: MST_$WIRE_AREA call */

    /* Enable histogram collection */
    PCHIST_$CONTROL.histogram_enabled = -1;  /* 0xFF = enabled */

    /* Set alignment mode if command 3 */
    PCHIST_$DOALIGN = (cmd == 3) ? -1 : 0;

    /* Copy histogram data to output */
    copy_histogram_data(data_ptr);

    /*
     * If command 0, increment system profiling count
     */
    if (cmd == 0) {
        ML_$EXCLUSION_START(&PCHIST_$CONTROL.lock);
        PCHIST_$CONTROL.sys_profiling_count++;
        PCHIST_$ENABLE_TERMINAL(0);  /* enabling = 0 */
        ML_$EXCLUSION_STOP(&PCHIST_$CONTROL.lock);
    }
}
