/*
 * sio2681_set_baud_rate - Internal helper to set baud rate
 *
 * Sets the baud rate for a channel by programming the Clock Select
 * Register (CSR) and Auxiliary Control Register (ACR).
 *
 * Original address: 0x00e1d1da
 */

#include "sio2681/sio2681_internal.h"

/*
 * sio2681_set_baud_rate - Program baud rate registers
 *
 * The 2681 supports multiple baud rates selected by a combination of:
 *   - ACR bit 7: Selects between two baud rate sets
 *   - CSR: 4-bit transmit rate + 4-bit receive rate
 *
 * This function looks up the baud rate codes in the global tables
 * and programs the hardware registers.
 *
 * Assembly analysis (FUN_00e1d1da):
 *   - Gets chip structure from channel[+4]
 *   - Updates ACR shadow bit 7 based on 'extended' parameter
 *   - Looks up CSR codes for tx_rate and rx_rate
 *   - Writes ACR to register offset 0x09
 *   - Writes CSR to register offset 0x03
 *   - Updates baud_support field in channel structure
 *
 * Parameters:
 *   channel  - Channel structure
 *   tx_rate  - Transmit baud rate index (0-16)
 *   rx_rate  - Receive baud rate index (0-16)
 *   extended - Use extended baud rate set (ACR bit 7)
 *              Negative = extended, non-negative = standard
 */
void sio2681_set_baud_rate(sio2681_channel_t *channel,
                           int16_t tx_rate, int16_t rx_rate, int8_t extended)
{
    sio2681_chip_t *chip;
    uint8_t acr_val;
    uint8_t csr_val;
    uint8_t tx_code;
    uint8_t rx_code;

    chip = channel->chip;

    /*
     * Update ACR bit 7 for baud rate set selection.
     * The ACR value is shadowed in the chip structure at offset +4.
     *
     * Assembly: andi.b #0x7f,(0x4,A1); move.b D0b,D1b; andi.b #-0x80,D1b; or.b
     */
    acr_val = (*(uint8_t *)&chip->config1) & 0x7F;  /* Clear bit 7 */
    if (extended < 0) {
        acr_val |= 0x80;  /* Set bit 7 for extended rates */
    }
    *(uint8_t *)&chip->config1 = acr_val;

    /*
     * Look up CSR codes from baud rate tables.
     * The baud_codes table maps rate index to CSR nibble value.
     *
     * Assembly shows:
     *   lea (0x0,A5,D1w*0x1),A2  ; A5 = data base, D1 = tx_rate * 2
     *   move.b (0x85,A2),D1b     ; Get tx CSR code
     *   move.b (0x85,A3),D1b     ; Get rx CSR code (shifted)
     */
    tx_code = SIO2681_$DATA.baud_codes[tx_rate] & 0x0F;
    rx_code = SIO2681_$DATA.baud_codes[rx_rate] & 0x0F;

    /* Combine into CSR value: upper nibble = rx, lower nibble = tx */
    csr_val = (rx_code << 4) | tx_code;

    /* Write ACR to hardware */
    chip->regs[SIO2681_REG_ACR] = acr_val;

    /* Write CSR to hardware */
    channel->regs[SIO2681_REG_CSRA] = csr_val;

    /*
     * Update baud_support field in channel structure.
     * This records which baud rate index is currently active.
     *
     * Assembly: move.w (0x62,A2),(0x1a,A0)
     */
    channel->baud_support = SIO2681_$DATA.baud_bits[tx_rate];
}
