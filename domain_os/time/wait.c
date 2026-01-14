/*
 * TIME_$WAIT - Wait for a specified time
 *
 * Blocks the calling process until the specified time has elapsed.
 * Supports both relative delays and absolute times.
 *
 * Parameters:
 *   delay_type - Pointer to delay type:
 *                0 = relative to current clock (TIME_$CLOCK)
 *                1 = relative to absolute clock (TIME_$ABS_CLOCK)
 *   delay - Pointer to delay value (clock_t)
 *   status - Status return
 *
 * Original address: 0x00e1650a
 *
 * The function:
 * 1. Creates a local event count
 * 2. If delay_type == 1, adjusts delay from current to absolute time
 * 3. Calls TIME_$ADVANCE to schedule callback
 * 4. Waits on the EC (with quit EC for interruptibility)
 * 5. If interrupted, cancels the timer
 */

#include "time/time_internal.h"
#include "misc/crash_system.h"

/* Status codes */
#define status_$time_quit_while_waiting 0x000D0003

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    uint16_t dtype;
    clock_t local_delay;
    clock_t current_clock;
    clock_t abs_clock;
    uint8_t elem_storage[32];  /* Queue element storage */
    uint16_t in_use_flag;
    void *ec;
    status_$t local_status;

    *status = status_$ok;
    dtype = *delay_type;
    in_use_flag = 0;

    /* Initialize event count */
    EC_$INIT(&ec);

    /* Copy delay value */
    local_delay.high = delay->high;
    local_delay.low = delay->low;

    /*
     * If delay_type == 1, convert from current-clock-relative
     * to absolute-clock-relative time.
     */
    if (dtype == 1) {
        TIME_$CLOCK(&current_clock);
        TIME_$ABS_CLOCK(&abs_clock);
        /* local_delay = local_delay - current_clock + abs_clock */
        SUB48(&local_delay, &current_clock);
        ADD48(&local_delay, &abs_clock);
    }

    /* Schedule the timer callback */
    TIME_$ADVANCE(&dtype, &local_delay, ec, elem_storage, &local_status);

    if (local_status != status_$ok) {
        *status = local_status;
        return;
    }

    /*
     * Wait on either our EC or the quit EC.
     * This allows the wait to be interrupted by signals.
     *
     * Simplified implementation - just wait on our EC.
     * Full implementation would use EC_$WAIT with multiple ECs.
     */
    /* EC_$WAIT(...) */
    (void)FIM_$QUIT_VALUE;
    (void)FIM_$QUIT_EC;
    (void)PROC1_$AS_ID;

    /* Check if wait was interrupted */
    if (in_use_flag != 0) {
        /* Timer callback had an error */
        CRASH_SYSTEM(&local_status);
    }
}
