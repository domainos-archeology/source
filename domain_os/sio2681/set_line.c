/*
 * SIO2681_$SET_LINE - Set line parameters
 *
 * Configures serial line parameters including baud rate, data format,
 * parity, stop bits, and flow control settings.
 *
 * Original address: 0x00e1d250
 */

#include "sio2681/sio2681_internal.h"

/*
 * SIO2681_$SET_LINE - Configure serial port parameters
 *
 * This is a complex function that handles multiple parameter types:
 *   - Baud rate (mask bits 0-1)
 *   - Character size (mask bit 2)
 *   - Stop bits (mask bit 3)
 *   - Parity (mask bit 4)
 *   - Flow control RTS/CTS (mask bits 5-6)
 *
 * Assembly analysis shows:
 *   - Acquires spin lock
 *   - Issues reset commands to reset MR pointer, receiver, transmitter
 *   - Processes baud rate change if mask & 0x03
 *   - Processes format change if mask & 0x41C (char size, stop, parity)
 *   - Processes flow control if mask & 0x60
 *   - Issues enable command
 *   - Releases spin lock
 *
 * Parameters:
 *   channel     - Channel structure
 *   params      - New parameters
 *   change_mask - Mask of parameters to change
 *   status_ret  - Status return
 */
void SIO2681_$SET_LINE(sio2681_channel_t *channel, sio_params_t *params,
                       uint32_t change_mask, status_$t *status_ret)
{
    sio2681_chip_t *chip;
    sio2681_channel_t *peer;
    volatile uint8_t *regs;
    uint8_t mr1_val;
    uint8_t mr2_val;
    ml_$spin_token_t token;

    *status_ret = 0;

    token = ML_$SPIN_LOCK(&SIO2681_$DATA.spin_lock);

    regs = channel->regs;

    /*
     * Issue reset commands to prepare for configuration.
     * The sequence resets the MR pointer, receiver, and transmitter.
     */
    regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_rx;      /* 0x20: Reset RX */
    regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_tx;      /* 0x30: Reset TX */
    regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_mr;      /* 0x10: Reset MR ptr */

    /* Clear the channel B indicator flag temporarily */
    channel->flags &= ~0x01;

    /*
     * Process baud rate change (mask bits 0-1)
     */
    if (change_mask & 0x03) {
        uint16_t tx_rate = (uint16_t)(params->baud_rate >> 16);
        uint16_t rx_rate = (uint16_t)(params->baud_rate & 0xFFFF);

        /* Validate baud rate indices */
        if (tx_rate <= 16 && rx_rate <= 16) {
            peer = channel->peer;

            /* Check if this baud rate is supported */
            uint16_t baud_bit = SIO2681_$DATA.baud_bits[rx_rate];

            if (baud_bit == 0 ||
                (!(change_mask & 0x02) && (peer->baud_support & baud_bit) == 0)) {
                /* Unsupported baud rate */
                *status_ret = status_$sio2681_invalid_baud;
            } else {
                chip = channel->chip;

                /*
                 * Determine if we need extended baud rates.
                 * Check the chip's config to see which set is available.
                 */
                uint16_t baud_mask;
                int8_t extended;

                /* Use channel-appropriate baud mask based on chip config */
                if (*(int16_t *)&chip->config1 < 0) {
                    baud_mask = SIO2681_$DATA.baud_mask_b;
                    extended = -1;
                } else {
                    baud_mask = SIO2681_$DATA.baud_mask_a;
                    extended = 0;
                }

                /* Check if the baud rate requires extended set */
                int8_t need_extended = ((baud_mask & baud_bit) != 0) ? -1 : 0;

                sio2681_set_baud_rate(channel, rx_rate, tx_rate, need_extended);

                /* Check if peer channel also needs updating */
                if ((peer->baud_support & baud_mask) == 0) {
                    /* Set peer to default baud rate */
                    uint16_t default_tx = (uint16_t)(SIO2681_$DATA.default_baud >> 16);
                    uint16_t default_rx = (uint16_t)(SIO2681_$DATA.default_baud & 0xFFFF);
                    sio2681_set_baud_rate(peer, default_rx, default_tx, extended);
                }
            }
        }
    }

    /*
     * Process character format change (mask & 0x41C)
     * Bit 2: char size, bit 3: stop bits, bit 4: parity, bit 10: special
     */
    if (change_mask & 0x41C) {
        /* Start with template values */
        mr1_val = (uint8_t)(SIO2681_$DATA.mr1_template >> 8);

        /*
         * Set parity mode (params->parity at offset 0x14)
         * 0 = none, 1 = odd, 2 = even, 3 = mark/space
         */
        switch (params->parity) {
        case 3:  /* Mark/space - disable parity */
            mr1_val &= 0xE3;
            break;
        case 1:  /* Odd parity */
            mr1_val = (mr1_val & 0xE7) | 0x04;
            break;
        case 0:  /* No parity */
            mr1_val = (mr1_val & 0xE7) | 0x10;
            break;
        default:  /* Even parity (case 2) */
            break;
        }

        /*
         * Set character size (params->char_size at offset 0x10)
         * 0 = 5 bits, 1 = 6 bits, 2 = 7 bits, 3 = 8 bits
         */
        switch (params->char_size) {
        case 0:  /* 5 bits */
            mr1_val &= 0xFC;
            break;
        case 1:  /* 6 bits */
            mr1_val = (mr1_val & 0xFC) | 0x01;
            break;
        case 2:  /* 7 bits */
            mr1_val = (mr1_val & 0xFC) | 0x02;
            break;
        case 3:  /* 8 bits */
        default:
            mr1_val |= 0x03;
            break;
        }

        /* Start with MR2 template */
        mr2_val = (uint8_t)(SIO2681_$DATA.mr2_template >> 8);

        /* Check for CTS flow control enable in flags2 (bit 1) */
        if (((uint8_t *)&params->flags2)[3] & 0x02) {
            mr2_val |= SIO2681_MR2_CTS_TX_CONTROL;  /* 0x10 */
        } else {
            mr2_val &= ~SIO2681_MR2_CTS_TX_CONTROL;
        }

        /*
         * Set stop bits (params->stop_bits at offset 0x12)
         * 1 = 1 stop bit, 2 = 1.5 stop bits, 3 = 2 stop bits
         */
        switch (params->stop_bits) {
        case 1:  /* 1 stop bit */
            mr2_val = (mr2_val & 0xE0) | SIO2681_MR2_STOP_1;
            break;
        case 2:  /* 1.5 stop bits */
            mr2_val = (mr2_val & 0xE0) | SIO2681_MR2_STOP_1_5;
            break;
        case 3:  /* 2 stop bits */
        default:
            mr2_val |= SIO2681_MR2_STOP_2;
            break;
        }

        /*
         * Write mode registers.
         * Must reset MR pointer first, then write MR1, then MR2.
         */
        regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_reset_error;  /* 0x40: Reset error */
        regs[SIO2681_REG_MRA] = mr1_val;
        regs[SIO2681_REG_MRA] = mr2_val;
    }

    /*
     * Process flow control settings (mask & 0x60)
     * Bit 5: RTS control, bit 6: DTR control
     */
    if (change_mask & 0x60) {
        chip = channel->chip;
        uint8_t opcr_val;
        uint8_t flags1_byte = ((uint8_t *)&params->flags1)[3];

        /*
         * Set RTS and DTR through output port.
         * Channel A uses bits 3/1, Channel B uses bits 2/0.
         */
        if (channel->flags & SIO2681_FLAG_CHANNEL_B) {
            /* Channel B: RTS is bit 2, DTR is bit 0 */
            opcr_val = (*(uint8_t *)&chip->config2) & 0xFB;
            if (flags1_byte & 0x08) {
                opcr_val |= 0x04;  /* Assert RTS */
            }

            opcr_val &= 0xFE;
            if (flags1_byte & 0x01) {
                opcr_val |= 0x01;  /* Assert DTR */
            }
        } else {
            /* Channel A: RTS is bit 3, DTR is bit 1 */
            opcr_val = (*(uint8_t *)&chip->config2) & 0xF7;
            if (flags1_byte & 0x08) {
                opcr_val |= 0x08;  /* Assert RTS */
            }

            opcr_val &= 0xFD;
            if (flags1_byte & 0x01) {
                opcr_val |= 0x02;  /* Assert DTR */
            }
        }

        *(uint8_t *)&chip->config2 = opcr_val;

        /* Write to output port set/reset registers */
        chip->regs[SIO2681_REG_SOPBC] = opcr_val;
        chip->regs[SIO2681_REG_ROPBC] = opcr_val ^ 0xFF;
    }

    /* Re-enable receiver and transmitter */
    regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_enable_rx_tx;  /* 0x05 */

    ML_$SPIN_UNLOCK(&SIO2681_$DATA.spin_lock, token);
}
