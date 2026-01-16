/*
 * RING_$IOCTL - I/O control for ring unit
 *
 * Performs I/O control operations on a ring unit.
 * Currently supports command 0: set transmit mask.
 *
 * Original address: 0x00E76B2C
 *
 * Assembly analysis:
 *   - Validates unit < 2
 *   - Command 0: set tmask from cmd[1]
 *   - Other commands: return error
 */

#include "ring/ring_internal.h"

/*
 * RING_$IOCTL - I/O control
 *
 * @param unit_ptr      Pointer to unit number
 * @param cmd           Pointer to command word
 *                      cmd[0] = command (0 = set tmask)
 *                      cmd[1] = parameter (tmask value for cmd 0)
 * @param param         Additional parameter (unused)
 * @param status_ret    Output: status code
 */
void RING_$IOCTL(uint16_t *unit_ptr, int16_t *cmd, void *param,
                 status_$t *status_ret)
{
    uint16_t unit_num;

    (void)param;

    unit_num = *unit_ptr;

    /*
     * Validate unit number.
     */
    if (unit_num > 1) {
        *status_ret = status_$ring_invalid_unit_num;
        return;
    }

    /*
     * Dispatch based on command.
     */
    switch (cmd[0]) {
    case 0:
        /*
         * Command 0: Set transmit mask.
         * The new mask value is in cmd[1].
         */
        ring_$set_hw_mask(unit_num, (uint16_t)cmd[1]);
        *status_ret = status_$ok;
        break;

    default:
        /*
         * Unknown command.
         */
        *status_ret = status_$ring_invalid_ioctl;
        break;
    }
}

/*
 * RING_$SET_TMASK - Set transmit mask (public interface)
 *
 * Sets the transmit mask register for a ring unit.
 * This is a simpler interface than RING_$IOCTL.
 *
 * Original address: 0x00E768E8
 *
 * @param unit          Unit number
 * @param mask          Transmit mask value
 */
void RING_$SET_TMASK(uint16_t unit, uint16_t mask)
{
    ring_unit_t *unit_data;

    if (unit > 1) {
        return;
    }

    unit_data = &RING_$DATA.units[unit];

    /*
     * Update the mask value.
     */
    unit_data->tmask = mask;

    /*
     * Apply to hardware.
     */
    ring_$set_hw_mask(unit, mask);
}

/*
 * RING_$KICK_DRIVER - Kick the ring driver
 *
 * Forces the driver to re-check for pending work.
 * This is useful when packets have been queued and the
 * driver may be idle.
 *
 * Original address: 0x00E768A8
 */
void RING_$KICK_DRIVER(void)
{
    /*
     * TODO: Implement driver kick logic.
     * This typically involves advancing an event count
     * or setting a flag to wake the driver process.
     */
}
