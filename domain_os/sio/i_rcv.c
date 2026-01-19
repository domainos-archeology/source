/*
 * SIO_$I_RCV - Receive interrupt handler
 *
 * Called when data is received on a serial port. This function handles
 * interrupt mask filtering and dispatches to the appropriate receive
 * handler based on the current configuration.
 *
 * The function performs the following:
 * 1. If error_flags is non-zero:
 *    - Merges errors with pending interrupt mask (filtered by int enable mask)
 *    - If receive error notification is enabled and callback exists, calls it
 *    - If pending interrupts exist and data_rcv handler exists, calls it
 * 2. Calls the default receive handler or data receive handler with the char
 *
 * Original address: 0x00e1c620
 */

#include "sio/sio_internal.h"

/*
 * Accessor macros for byte-level access to descriptor fields
 * These are needed because some flags are accessed at byte granularity
 */
#define SIO_DESC_CTRL(desc)     (*(uint8_t *)((char *)(desc) + 0x53))
#define SIO_DESC_INT_MASK(desc) (*(uint32_t *)((char *)(desc) + 0x54))
#define SIO_DESC_STATUS(desc)   (*(uint8_t *)((char *)(desc) + 0x67))

void SIO_$I_RCV(sio_desc_t *desc, uint8_t char_data, uint32_t error_flags)
{
    void (*handler)(uint32_t owner, uint8_t data);
    uint32_t owner;

    if (error_flags != 0) {
        /*
         * Merge error flags with pending interrupts
         * Only keep errors that are enabled in the interrupt mask
         */
        desc->pending_int |= (SIO_DESC_INT_MASK(desc) & error_flags);

        /*
         * Check if receive error notification is enabled (bit 5 in status)
         * and receive error control is enabled (bit 3 in ctrl)
         * and special receive handler exists
         */
        if ((SIO_DESC_STATUS(desc) & SIO_STAT_RECV_ERROR) != 0 &&
            (SIO_DESC_CTRL(desc) & SIO_CTRL_RECV_ERROR) != 0 &&
            desc->special_rcv != 0) {
            /* Call special receive handler */
            ((void (*)(uint32_t))desc->special_rcv)(desc->owner);
        }

        /*
         * If pending interrupts exist and data receive handler exists,
         * dispatch to data receive handler with the character
         */
        if (desc->pending_int != 0 && desc->data_rcv != 0) {
            owner = desc->owner;
            handler = (void (*)(uint32_t, uint8_t))desc->data_rcv;
            goto call_handler;
        }
    }

    /* Call default receive handler */
    owner = desc->owner;
    handler = (void (*)(uint32_t, uint8_t))desc->rcv_handler;

call_handler:
    handler(owner, char_data);
}
