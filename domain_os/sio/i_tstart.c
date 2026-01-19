/*
 * SIO_$I_TSTART - Start/continue transmission
 *
 * Main transmit state machine. Pulls characters from the transmit
 * buffer and sends them to the output device. Handles special
 * sequences for delays and break signals.
 *
 * Transmit buffer format:
 *   - Normal characters are sent directly
 *   - 0xFE followed by 0x00 followed by 2-byte delay = timed delay
 *   - 0xFE followed by other = break signal
 *
 * Flow control:
 *   - Checks state bits before transmitting
 *   - Respects CTS blocking, inhibit states, and defer flags
 *
 * Original address: 0x00e1c7a8
 */

#include "sio/sio_internal.h"

/* Accessor macros for byte-level access */
#define SIO_DESC_STATE(desc)       (*(uint16_t *)((char *)(desc) + 0x74))
#define SIO_DESC_XMIT_STATE(desc)  (*(uint8_t *)((char *)(desc) + 0x75))

/*
 * SIO_DELAY_RESTART - Callback from time subsystem when delay expires
 *
 * Called by TIME_$Q_ADD_CALLBACK when a transmit delay completes.
 * Just restarts transmission.
 *
 * Original address: 0x00e1c690
 */
uint16_t SIO_DELAY_RESTART(sio_desc_t **args)
{
    sio_desc_t *desc = *args;

    /* Clear delay-active flag (bit 4 in state) */
    SIO_DESC_STATE(desc) &= ~SIO_STATE_DELAY_ACTIVE;

    /* Resume transmission */
    return SIO_$I_TSTART(desc);
}

uint16_t SIO_$I_TSTART(sio_desc_t *desc)
{
    sio_txbuf_t *txbuf;
    uint16_t read_idx;
    uint16_t result = 0;
    int8_t char_data;
    status_$t status;
    clock_t now;
    uint16_t delay_ticks;
    sio_desc_t *desc_ptr;

    /*
     * Check if any blocking conditions exist:
     * - Bits 0-4 of state (delay active, break, etc.)
     */
    if ((SIO_DESC_STATE(desc) & 0x1F) != 0) {
        return result;
    }

    /*
     * Check bits 5-6 of state (break active/pending flags)
     */
    if ((SIO_DESC_STATE(desc) & 0x60) != 0) {
        if ((SIO_DESC_XMIT_STATE(desc) & SIO_XMIT_DEFER_PENDING) == 0) {
            /*
             * Send break signal (character code 0x11)
             */
            ((void (*)(uint32_t, uint16_t))desc->output_char)(desc->context, 0x1100);
            SIO_DESC_STATE(desc) &= ~0x60;  /* Clear break flags */
        } else {
            /*
             * Deferred operation - send 0x13 and update state
             */
            ((void (*)(uint32_t, uint16_t))desc->output_char)(desc->context, 0x1300);
            SIO_DESC_XMIT_STATE(desc) |= SIO_XMIT_DEFER_COMPLETE;
            SIO_DESC_XMIT_STATE(desc) &= ~SIO_XMIT_DEFER_PENDING;
        }
        return result;
    }

    /*
     * Normal transmit path - get transmit buffer
     */
    txbuf = (sio_txbuf_t *)desc->txbuf;
    read_idx = txbuf->read_idx;

    /* Check if buffer is empty */
    if (read_idx == txbuf->write_idx) {
        return result;
    }

    /* Mark transmit as active */
    SIO_DESC_XMIT_STATE(desc) |= SIO_XMIT_ACTIVE;

    /* Get next character from circular buffer */
    char_data = txbuf->data[read_idx - 1];  /* -1 because index is 1-based in buffer */

    /* Advance read index (circular) */
    if (read_idx == txbuf->size) {
        txbuf->read_idx = 1;
    } else {
        txbuf->read_idx = read_idx + 1;
    }

    /*
     * Check for special delay marker (0xFE)
     */
    if ((uint8_t)char_data == SIO_TSTART_DELAY_MARKER) {
        /* Get command byte */
        read_idx = txbuf->read_idx;
        char_data = txbuf->data[read_idx - 1];

        if (read_idx == txbuf->size) {
            txbuf->read_idx = 1;
        } else {
            txbuf->read_idx = read_idx + 1;
        }

        if (char_data == SIO_TSTART_DELAY_CMD) {
            /*
             * Delay command - read 2-byte delay value (big-endian)
             */
            uint32_t delay_value = 0;
            int i;

            for (i = 0; i < 2; i++) {
                read_idx = txbuf->read_idx;
                delay_value = (delay_value << 8) | txbuf->data[read_idx - 1];

                if (read_idx == txbuf->size) {
                    txbuf->read_idx = 1;
                } else {
                    txbuf->read_idx = read_idx + 1;
                }
            }

            /*
             * Convert delay to clock ticks
             * Multiply by 0xFA (250) to convert ms to 4us ticks
             */
            delay_ticks = M_MIU_LLW(delay_value, 0xFA);

            /* Set delay-active flag */
            SIO_DESC_STATE(desc) = (SIO_DESC_STATE(desc) & 0xFFFE) | SIO_STATE_DELAY_ACTIVE;

            /* Get current time */
            TIME_$ABS_CLOCK(&now);

            /* Schedule callback when delay expires */
            desc_ptr = desc;
            result = TIME_$Q_ADD_CALLBACK(
                &TIME_$RTEQ,
                &delay_ticks,   /* This should be a delay struct but simplified here */
                0,              /* relative */
                &now,
                (void *)SIO_DELAY_RESTART,
                desc,
                8,              /* flags */
                &SIO_DELAY_RESTART_QUEUE_ELEM,
                (time_queue_elem_t *)(desc + 2),  /* queue elem storage in desc */
                &status
            );

            if (status != status_$ok) {
                /*
                 * If scheduling failed, call restart directly
                 */
                result = SIO_DELAY_RESTART(&desc_ptr);
            }

            return result;
        }
    }

    /*
     * Normal character - send it to the output device
     */
    ((void (*)(uint32_t, uint8_t))desc->output_char)(desc->context, char_data);

    /*
     * Check if buffer is getting low - wake writer if waiting
     * Buffer low threshold is 0x10 (16) characters remaining
     */
    result = txbuf->write_idx - txbuf->read_idx;
    if ((int16_t)result < 0) {
        result += txbuf->size;
    }

    if (result == 0x10 || result == 0) {
        /* Call drain handler to notify buffer space available */
        ((void (*)(uint32_t))desc->drain_handler)(desc->owner);
    }

    return result;
}
