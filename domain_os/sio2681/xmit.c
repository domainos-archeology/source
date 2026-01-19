/*
 * SIO2681_$XMIT - Transmit a character
 *
 * Writes a character to the transmit holding register and enables
 * the transmit interrupt if not already enabled.
 *
 * Original address: 0x00e1d4fc
 */

#include "sio2681/sio2681_internal.h"

/*
 * SIO2681_$XMIT - Write character to transmitter
 *
 * This function is called to transmit a single character. It writes
 * to the THR and ensures the TxRDY interrupt is enabled so we get
 * notified when the character has been sent.
 *
 * Assembly analysis:
 *   - Writes character to THR at offset 0x07 of channel regs
 *   - Calculates interrupt bit from channel's int_bit field
 *   - If bit not already set in IMR shadow, sets it and writes IMR
 *
 * Parameters:
 *   channel - Channel structure
 *   ch      - Character to transmit
 */
void SIO2681_$XMIT(sio2681_channel_t *channel, uint8_t ch)
{
    sio2681_chip_t *chip;
    uint8_t int_bit;
    uint8_t imr_val;

    chip = channel->chip;

    /* Write character to Transmit Holding Register */
    channel->regs[SIO2681_REG_THRA] = ch;

    /*
     * Calculate the TxRDY interrupt bit for this channel.
     * Channel A uses bit 0, Channel B uses bit 4.
     * The int_bit field contains the bit position (0 or 4).
     */
    int_bit = 1 << (channel->int_bit & 0x1F);

    /* Check if TxRDY interrupt is already enabled */
    imr_val = chip->imr_shadow;
    if ((imr_val & int_bit) == 0) {
        /* Enable TxRDY interrupt */
        imr_val |= int_bit;
        chip->imr_shadow = imr_val;
        chip->regs[SIO2681_REG_IMR] = imr_val;
    }
}
