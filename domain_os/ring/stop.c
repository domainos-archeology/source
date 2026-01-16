/*
 * RING_$STOP - Stop a token ring unit
 *
 * Deactivates a ring unit. The unit can be restarted with RING_$START.
 * Acquires exclusion lock during the stop operation.
 *
 * Original address: 0x00E769C4
 *
 * Assembly analysis:
 *   - Validates unit number < 2
 *   - Acquires tx_exclusion lock
 *   - Checks if tmask is non-zero (unit actually running)
 *   - Clears RING_UNIT_RUNNING flag (bit 1)
 *   - Calls ring_$set_hw_mask with 0 to disable hardware
 *   - Releases tx_exclusion lock
 */

#include "ring/ring_internal.h"

/*
 * RING_$STOP - Stop a ring unit
 *
 * @param unit_ptr      Pointer to unit number
 * @param status_ret    Output: status code
 */
void RING_$STOP(uint16_t *unit_ptr, status_$t *status_ret)
{
    uint16_t unit_num;
    ring_unit_t *unit_data;

    unit_num = *unit_ptr;

    /*
     * Validate unit number (must be 0 or 1).
     */
    if (unit_num > 1) {
        *status_ret = status_$ring_invalid_unit_num;
        return;
    }

    /*
     * Get unit data structure.
     */
    unit_data = &RING_$DATA.units[unit_num];

    /*
     * Acquire transmit exclusion lock.
     * This ensures no transmission is in progress.
     */
    ML_$EXCLUSION_START(&unit_data->tx_exclusion);

    /*
     * Check if unit is actually running.
     * tmask is non-zero when running.
     */
    if (unit_data->tmask == 0) {
        /*
         * Unit not running - release lock and return error.
         */
        ML_$EXCLUSION_STOP(&unit_data->tx_exclusion);
        *status_ret = status_$ring_device_offline;
        return;
    }

    /*
     * Clear the RUNNING flag (bit 1).
     */
    unit_data->state_flags &= ~RING_UNIT_RUNNING;

    /*
     * Disable hardware by setting transmit mask to 0.
     */
    ring_$set_hw_mask(unit_num, 0);

    /*
     * Release exclusion lock.
     */
    ML_$EXCLUSION_STOP(&unit_data->tx_exclusion);

    *status_ret = status_$ok;
}
