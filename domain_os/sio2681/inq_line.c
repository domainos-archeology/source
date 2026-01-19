/*
 * SIO2681_$INQ_LINE - Inquire line status
 *
 * Returns current modem signal states (CTS, DCD) for a channel.
 *
 * Original address: 0x00e725b0
 */

#include "sio2681/sio2681_internal.h"

/*
 * SIO2681_$INQ_LINE - Query modem signal states
 *
 * Reads the Input Port Register (IPR) to determine current CTS and DCD
 * signal states and updates the parameters block accordingly.
 *
 * Assembly analysis:
 *   - Checks mask bits 0x100 (CTS) and 0x80 (DCD)
 *   - Reads IPR at offset 0x1B from chip base
 *   - Updates params_ret flags based on signal states
 *   - Channel A uses bits 1 (CTS) and 3 (DCD)
 *   - Channel B uses bits 0 (CTS) and 2 (DCD)
 *
 * Parameters:
 *   channel    - Channel structure
 *   params_ret - Parameter block to update (flags at offset 0x00)
 *   mask       - Bit 8 = query CTS, bit 7 = query DCD
 *   status_ret - Status return (always 0)
 */
void SIO2681_$INQ_LINE(sio2681_channel_t *channel, sio_params_t *params_ret,
                       uint32_t mask, status_$t *status_ret)
{
    *status_ret = 0;

    /* Check if we need to query any signals */
    if ((mask & 0x180) == 0) {
        return;
    }

    /*
     * Read Input Port Register from chip base.
     * The IPR is at offset 0x1B and is shared between both channels.
     */
    sio2681_chip_t *chip = channel->chip;
    uint8_t ipr = chip->regs[SIO2681_REG_IPR];
    uint8_t cts_bit, dcd_bit;

    /*
     * Select the appropriate bits based on channel.
     * The flags field at offset 0x19 indicates which channel:
     *   Bit 1 set = channel B, clear = channel A
     *
     * Channel A: CTS is bit 1, DCD is bit 3
     * Channel B: CTS is bit 0, DCD is bit 2
     */
    if (channel->flags & SIO2681_FLAG_CHANNEL_B) {
        /* Channel B */
        cts_bit = (ipr & SIO2681_IPCR_CTS_B) ? 0 : 1;  /* Inverted: 0 = asserted */
        dcd_bit = (ipr & SIO2681_IPCR_DCD_B) ? 0 : 1;  /* Inverted: 0 = asserted */
    } else {
        /* Channel A */
        cts_bit = (ipr & SIO2681_IPCR_CTS_A) ? 0 : 1;  /* Bit 1 */
        dcd_bit = (ipr & SIO2681_IPCR_DCD_A) ? 0 : 1;  /* Bit 3 */
    }

    /*
     * Update CTS state in flags1 (bit 1 = CTS asserted)
     */
    if (mask & 0x100) {
        uint8_t *flags_byte = (uint8_t *)&params_ret->flags1 + 3;  /* Low byte */
        if (cts_bit) {
            *flags_byte |= 0x02;  /* Set bit 1 */
        } else {
            *flags_byte &= ~0x02;  /* Clear bit 1 */
        }
    }

    /*
     * Update DCD state in flags1 (bit 2 = DCD asserted)
     * Also check for DCD notification bit in flags2
     */
    if (mask & 0x80) {
        uint8_t *flags_byte = (uint8_t *)&params_ret->flags1 + 3;
        uint8_t flags2_byte = *((uint8_t *)&params_ret->flags2 + 3);

        if (dcd_bit || (flags2_byte & 0x40)) {
            /* DCD is asserted or notification pending */
            *flags_byte |= 0x04;  /* Set bit 2 */
        } else {
            *flags_byte &= ~0x04;  /* Clear bit 2 */
        }
    }

    /* OR in the reserved field at offset 0x14 */
    params_ret->flags1 |= channel->reserved_14;
}
