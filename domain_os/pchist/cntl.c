/*
 * PCHIST_$CNTL - Control system-wide PC histogram
 *
 * Re-emitted from the image (0x00E5CDB6..0x00E5CFA8, 500 bytes) and verified
 * block by block.  A5 = 0xE2C204 (PCHIST_$CONTROL), A2 = 0xE85718 so that
 * (0x50C,A2) = PCHIST_$HISTOGRAM (0xE85C24) and (0x534,A2) = histogram[0].
 *
 * Frame (link.w A6,-0x2C): (0x8,A6) cmd ptr, (0xC,A6) range ptr -> A3,
 * (0x10,A6) data ptr -> D5, (0x14,A6) status ptr -> A4 (cleared first).
 * PCHIST_$STOP_PROFILING (0x00E5CD66) and PCHIST_$UNWIRE_CLEANUP
 * (0x00E5CD02) are nested procedures reached with this frame as their
 * static link (`movea.l (A6),A2` / `movea.l A6,A1`); STOP_PROFILING reads
 * this frame's cmd ptr through it.
 *
 * Range arithmetic (0x00E5CDE6..0x00E5CE44):
 *   size = (start == 0 && end == 0) ? 0 : (end < start ? 0x200 : end - start + 1)
 *   size == 0 -> multiplier 0x100, bucket 0x1000000, shift 0x18
 *   else buckets = size >> 8 (+1 if size & 0xFF); bucket = 2, shift = 1,
 *        doubled while buckets > bucket; multiplier = (size + bucket - 1)
 *        >> shift, and 0x100 when its LOW WORD is zero (tst.w D2w).
 */

#include "pchist/pchist_internal.h"
#include "mst/mst.h"
#include "math/math.h"
#include "arch/arch.h"

/*
 * Constant cell in this module's code region, passed by address with
 * `pea (0xac,PC)` at 0x00E5CEFC: the maximum number of pages
 * MST_$WIRE_AREA may add to PCHIST_$WIRE_PAGES.
 */
static const int16_t pchist_$max_wire_pages_00e5cfaa = 3;

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
            PCHIST_$STOP_PROFILING(cmd_ptr);
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
    PCHIST_$STOP_PROFILING(cmd_ptr);

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
            /* 0x00E5CE3E: tst.w D2w -- only the low word is tested */
            if ((uint16_t)multiplier == 0) {
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
     * Negative value means it's a UPID that needs to be converted.
     * 0x00E5CE6C `tst.w (0x8,A3)` / 0x00E5CE78 `move.w (0x8,A3)`: the WORD
     * at range +8, i.e. the big-endian HIGH half of range_ptr[2].
     */
    pid_param = (int16_t)(range_ptr[2] >> 16);
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
    /* 0x00E5CEEC: st (0x50e,A2) -- the single byte at HISTOGRAM+0x02, the
     * HIGH byte of the doalign word; the low byte is left alone. */
    PCHIST_$HISTOGRAM.doalign = (int16_t)((PCHIST_$HISTOGRAM.doalign & 0x00FF) | 0xFF00);
    PCHIST_$HISTOGRAM.enabled = 1;   /* 0x00E5CEF0: move.w #1,(0x50c,A2) */

    /*
     * Wire the histogram buffer pages so the sampler can touch them from an
     * interrupt.  0x00E5CEF6:
     *
     *   pea     (0xe8604e).l          ; arg5 = &PCHIST_$WIRED_COUNT
     *   pea     (0xac,PC)             ; arg4 = &pchist_$max_wire_pages
     *                                 ;        (cell 0x00E5CFAA, value 3)
     *   pea     (0xe85c18).l          ; arg3 = &PCHIST_$WIRE_PAGES[1]
     *   lea     (0x934,A2),A0
     *   move.l  A0,(-0x28,A6) ; pea (-0x28,A6)
     *                                 ; arg2 = &end VA   (0x00E8604C)
     *   lea     (0x50c,A2),A1
     *   move.l  A1,(-0x2c,A6) ; pea (-0x2c,A6)
     *                                 ; arg1 = &start VA (0x00E85C24)
     *   jsr     MST_$WIRE_AREA
     *
     * A2 is 0x00E85718, so the wired range is A2+0x50C .. A2+0x934, i.e.
     * PCHIST_$HISTOGRAM and the 0x428 bytes it occupies.  Both VAs are
     * passed by address, through locals, exactly as here.
     */
    {
        uint32_t hist_start_va = ARCH_PTR_TO_VA(&PCHIST_$HISTOGRAM);
        uint32_t hist_end_va   = hist_start_va + (uint32_t)sizeof(PCHIST_$HISTOGRAM);

        MST_$WIRE_AREA(&hist_start_va, &hist_end_va,
                       &PCHIST_$WIRE_PAGES[1],
                       &pchist_$max_wire_pages_00e5cfaa,
                       &PCHIST_$WIRED_COUNT);
    }

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
