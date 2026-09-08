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
 * Five frame slots; see ring/ring.h for the full map and for why slot 0x16
 * is the status return.  Slots 0x10 (a word) and 0x12 (a longword) are never
 * read by this routine.
 *
 * @param unit_ptr      Pointer to unit number
 * @param cmd           cmd[0] = command (0 = set tmask)
 *                      cmd[1] = parameter (tmask value for cmd 0)
 * @param reserved      Never read (0x0088 from NETWORK_$SET_SERVICE)
 * @param param4        Never read
 * @param status_ret    Output: status code
 */
void RING_$IOCTL(uint16_t *unit_ptr, int16_t *cmd, uint16_t reserved,
                 void *param4, status_$t *status_ret)
{
    uint16_t unit_num;
    int16_t mask;

    (void)reserved;
    (void)param4;

    unit_num = *unit_ptr;

    /*
     * 0x00E76B46-0x00E76B52: validate unit number (cmpi.w #0x1 / bls, so
     * the test is unsigned and 0 and 1 pass).
     */
    if (unit_num > 1) {
        *status_ret = status_$ring_invalid_unit_num;    /* 0x00310002 */
        return;
    }

    /*
     * 0x00E76B54-0x00E76B6A: dispatch on cmd[0].
     */
    switch (cmd[0]) {
    case 0:
        /*
         * Command 0: set the transmit mask.  The image copies cmd[1] into
         * the frame temporary at A6-0x2 (0x00E76B58) and pushes that word
         * together with the unit word (0x00E76B5E-0x00E76B62).
         */
        mask = cmd[1];
        ring_$set_hw_mask(unit_num, (uint16_t)mask);
        *status_ret = status_$ok;                       /* 0x00E76B68 clr.l */
        break;

    default:
        /* 0x00E76B6C: unknown command */
        *status_ret = status_$ring_not_implemented;     /* 0x00310001 */
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

    unit_data = &RING_$CTL.units[unit];

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
     * TODO(source-6co): the body is empty.  RING_$KICK_DRIVER at 0x00E768A8
     * is 0x40 bytes; re-emit it against the image, which advances the
     * driver's eventcount through EC_$ADVANCE (0x00E206EE) after loading the
     * RING globals base with `lea (0xe86400).l,A5`.
     */
}
