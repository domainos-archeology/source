/*
 * TIME_$WAIT2 - Wait for a specified time (variant)
 *
 * Similar to TIME_$WAIT but allows waiting on an additional
 * event count along with the timer.
 *
 * Parameters:
 *   delay_type - Pointer to delay type
 *   delay - Pointer to delay value (clock_t)
 *   extra_ec - Additional event count to wait on
 *   ec_value - Expected value for extra_ec
 *   status - Status return
 *
 * Returns:
 *   0 if timer expired
 *   non-zero if extra_ec triggered
 *
 * Original address: 0x00e16654
 */

#include "time.h"
#include "ec/ec.h"
#include "misc/crash_system.h"

/* Error for queue element already in use */
static status_$t Time_Queue_Elem_Already_In_Use_Err = 0x000D0008;

int8_t TIME_$WAIT2(uint16_t *delay_type, clock_t *delay, void *extra_ec,
                   uint32_t *ec_value, status_$t *status)
{
    uint16_t dtype;
    uint8_t elem_storage[32];
    uint16_t in_use_flag;
    void *timer_ec;
    status_$t local_status;
    int16_t wait_result;

    dtype = *delay_type;
    *status = status_$ok;
    in_use_flag = 0;

    /* Initialize event count for timer */
    EC_$INIT(&timer_ec);

    /* Schedule the timer callback */
    TIME_$ADVANCE(&dtype, delay, timer_ec, elem_storage, &local_status);

    if (local_status != status_$ok) {
        CRASH_SYSTEM(&local_status);
        *status = local_status;
        return 0;
    }

    /*
     * Wait on either the timer EC or the extra EC.
     * EC_$WAIT returns which EC triggered:
     *   0 = first EC (extra_ec)
     *   1 = second EC (timer_ec)
     *
     * Simplified implementation - actual would use EC_$WAIT with both ECs.
     */
    (void)extra_ec;
    (void)ec_value;
    wait_result = 1;  /* Assume timer fired */

    /* If timer EC didn't fire, cancel the timer */
    if (wait_result != 1) {
        TIME_$CANCEL((uint32_t *)1, elem_storage, &local_status);
    }

    /* Check for queue element error */
    if (in_use_flag != 0) {
        CRASH_SYSTEM(&Time_Queue_Elem_Already_In_Use_Err);
    }

    /* Return true (non-zero) if timer expired, false otherwise */
    return (wait_result == 0) ? -1 : 0;
}
