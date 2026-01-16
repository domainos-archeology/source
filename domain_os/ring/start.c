/*
 * RING_$START - Start a token ring unit
 *
 * Activates a ring unit for transmission and reception.
 * Must be called after RING_$INIT has completed successfully.
 *
 * Original address: 0x00E76830
 *
 * Assembly analysis:
 *   - Validates unit number < 2 and unit is initialized
 *   - Calls ring_$disable_interrupts (FUN_00e7667c)
 *   - Sets RING_UNIT_RUNNING flag (bit 1) in state_flags
 *   - If RING_UNIT_STARTED flag not set, calls ring_$do_start (FUN_00e7671c)
 *   - Calls ring_$set_hw_mask with tmask | 0xBF
 */

#include "ring/ring_internal.h"

/*
 * RING_$START - Start a ring unit
 *
 * @param unit_ptr      Pointer to unit number
 * @param status_ret    Output: status code
 */
void RING_$START(uint16_t *unit_ptr, status_$t *status_ret)
{
    uint16_t unit_num;
    ring_unit_t *unit_data;
    uint16_t mask;

    unit_num = *unit_ptr;

    /*
     * Validate unit number (must be 0 or 1).
     */
    if (unit_num > 1) {
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Get unit data structure.
     */
    unit_data = &RING_$DATA.units[unit_num];

    /*
     * Check if unit is initialized.
     * The initialized flag is -1 (0xFF) when set.
     */
    if (unit_data->initialized >= 0) {
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Disable interrupts during startup.
     */
    ring_$disable_interrupts();

    /*
     * Set the RUNNING flag (bit 1) to indicate unit is active.
     */
    unit_data->state_flags |= RING_UNIT_RUNNING;

    /*
     * If unit hasn't been started before (STARTED flag not set),
     * perform the full start sequence.
     */
    if ((unit_data->state_flags & RING_UNIT_STARTED) == 0) {
        ring_$do_start(unit_num, unit_data, status_ret);
    }
    else {
        *status_ret = status_$ok;
    }

    /*
     * Configure hardware transmit mask.
     * Combine the current tmask with 0xBF.
     */
    mask = unit_data->tmask | 0xBF;
    ring_$set_hw_mask(unit_num, mask);
}
