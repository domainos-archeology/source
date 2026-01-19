/*
 * SIO2681_$INT - Interrupt handler for SIO2681 DUART
 *
 * This function handles all interrupts from a 2681 DUART chip.
 * It dispatches receive, transmit, and modem status changes to
 * the appropriate SIO handlers.
 *
 * Original address: 0x00e1ceec
 */

#include "sio2681/sio2681_internal.h"

/*
 * SIO2681_$INT - Interrupt handler
 *
 * The 2681 generates interrupts for:
 *   - Bit 0: Channel A TxRDY (transmitter ready)
 *   - Bit 1: Channel A RxRDY (receiver ready/FIFO not empty)
 *   - Bit 2: Channel A delta break
 *   - Bit 3: Counter ready
 *   - Bit 4: Channel B TxRDY
 *   - Bit 5: Channel B RxRDY
 *   - Bit 6: Channel B delta break
 *   - Bit 7: Input port change (CTS/DCD changes)
 *
 * Assembly analysis shows the interrupt loop reads ISR, masks with IMR,
 * and processes each set bit until all are handled.
 *
 * Parameters:
 *   chip - Pointer to chip structure containing channel pointers
 *          Layout: chip[0] = channel A, chip[1] = channel B, chip[2] = chip struct
 *
 * Note: The parameter is actually a pointer to an array of three pointers:
 *   [0] = channel A structure
 *   [1] = channel B structure
 *   [2] = chip structure (contains IMR shadow)
 */
void SIO2681_$INT(sio2681_chip_t *chip_array_ptr)
{
    /* The parameter is really an array: [chan_a, chan_b, chip] */
    sio2681_channel_t *chan_a = ((sio2681_channel_t **)chip_array_ptr)[0];
    sio2681_channel_t *chan_b = ((sio2681_channel_t **)chip_array_ptr)[1];
    sio2681_chip_t *chip = ((sio2681_chip_t **)chip_array_ptr)[2];

    volatile uint8_t *imr_shadow_ptr = &chip->imr_shadow;
    sio_desc_t *chan_a_sio = chan_a->sio_desc;
    sio_desc_t *chan_b_sio = chan_b->sio_desc;
    uint8_t pending;
    uint8_t status_byte;

    /*
     * Main interrupt loop - process until no more pending interrupts.
     * Read ISR and mask with IMR shadow to get enabled pending interrupts.
     */
interrupt_loop:
    pending = *imr_shadow_ptr & chip->regs[SIO2681_REG_ISR];

    if (pending == 0) {
        return;
    }

    /*
     * Channel B RxRDY (bit 5) - highest priority receive
     */
    if (pending & SIO2681_INT_RXRDY_B) {
        /* Read received character and status */
        status_byte = chan_b->regs[SIO2681_REG_SRA];  /* Status register */

        /* Extract error flags (upper nibble of status shifted) */
        uint32_t error_flags = SIO2681_$DATA.error_table[(status_byte >> 4) & 0x0F];

        /* Call SIO receive handler */
        SIO_$I_RCV(chan_b_sio, chan_b->regs[SIO2681_REG_RHRA], error_flags);

        if (pending == SIO2681_INT_RXRDY_B) {
            goto interrupt_loop;
        }
        pending &= ~SIO2681_INT_RXRDY_B;
    }

    /*
     * Channel A RxRDY (bit 1)
     */
    if (pending & SIO2681_INT_RXRDY_A) {
        status_byte = chan_a->regs[SIO2681_REG_SRA];
        uint32_t error_flags = SIO2681_$DATA.error_table[(status_byte >> 4) & 0x0F];

        SIO_$I_RCV(chan_a_sio, chan_a->regs[SIO2681_REG_RHRA], error_flags);

        if (pending == SIO2681_INT_RXRDY_A) {
            goto interrupt_loop;
        }
        pending &= ~SIO2681_INT_RXRDY_A;
    }

    /*
     * Channel A TxRDY (bit 0)
     */
    if (pending & SIO2681_INT_TXRDY_A) {
        if (chan_a->tx_int_mask == 0) {
            /* No more data to transmit - call transmit done handler */
            int8_t result = SIO_$I_XMIT_DONE(chan_a_sio);
            if (result >= 0) {
                /* Disable TxRDY interrupt */
                *imr_shadow_ptr &= ~SIO2681_INT_TXRDY_A;
                chip->regs[SIO2681_REG_IMR] = *imr_shadow_ptr;
            }
        } else {
            /* More data to transmit - record interrupt for profiling */
            PCHIST_$INTERRUPT((uint32_t *)((uint8_t *)chip_array_ptr + 0x0C));  /* pchist offset */
            SIO2681_$XMIT(chan_a, 0x20);  /* Continue transmission */
        }

        if (pending == SIO2681_INT_TXRDY_A) {
            goto interrupt_loop;
        }
        pending &= ~SIO2681_INT_TXRDY_A;
    }

    /*
     * Channel B TxRDY (bit 4)
     */
    if (pending & SIO2681_INT_TXRDY_B) {
        if (chan_b->tx_int_mask == 0) {
            int8_t result = SIO_$I_XMIT_DONE(chan_b_sio);
            if (result >= 0) {
                *imr_shadow_ptr &= ~SIO2681_INT_TXRDY_B;
                chip->regs[SIO2681_REG_IMR] = *imr_shadow_ptr;
            }
        } else {
            PCHIST_$INTERRUPT((uint32_t *)((uint8_t *)chip_array_ptr + 0x0C));
            SIO2681_$XMIT(chan_b, 0x20);
        }

        if (pending == SIO2681_INT_TXRDY_B) {
            goto interrupt_loop;
        }
        pending &= ~SIO2681_INT_TXRDY_B;
    }

    /*
     * Input port change (bit 7) - CTS and DCD changes
     */
    if (pending & SIO2681_INT_INPUT_CHANGE) {
        uint8_t ipcr = chip->regs[SIO2681_REG_IPCR];

        /* CTS A changed (bit 4) */
        if (ipcr & SIO2681_IPCR_DELTA_CTS_A) {
            /* CTS state is inverted: bit 0 clear = CTS asserted */
            int8_t cts_state = (ipcr & SIO2681_IPCR_CTS_A) ? 0 : -1;
            SIO_$I_CTS_CHANGE(chan_a_sio, cts_state);
        }

        /* CTS B changed (bit 5) */
        if (ipcr & SIO2681_IPCR_DELTA_CTS_B) {
            int8_t cts_state = (ipcr & SIO2681_IPCR_CTS_B) ? 0 : -1;
            SIO_$I_CTS_CHANGE(chan_b_sio, cts_state);
        }

        /* DCD A changed (bit 6) */
        if (ipcr & SIO2681_IPCR_DELTA_DCD_A) {
            /* DCD state: bit 2 clear = DCD asserted (active low) */
            int8_t dcd_state = (ipcr & SIO2681_IPCR_DCD_A) ? 0 : -1;
            SIO_$I_DCD_CHANGE(chan_a_sio, dcd_state);
        }

        /* DCD B changed (bit 7) */
        if (ipcr & SIO2681_IPCR_DELTA_DCD_B) {
            int8_t dcd_state = (ipcr & SIO2681_IPCR_DCD_B) ? 0 : -1;
            SIO_$I_DCD_CHANGE(chan_b_sio, dcd_state);
        }
    }

    goto interrupt_loop;
}
