/*
 * SIO_$I_CTS_CHANGE - CTS signal change handler
 *
 * Called when the Clear-To-Send signal changes state. Handles
 * hardware flow control by blocking or unblocking transmission
 * based on CTS state.
 *
 * If CTS becomes asserted (cts_state negative):
 *   - Clear CTS blocked flag and try to restart transmission
 * If CTS becomes deasserted and CTS flow control is enabled:
 *   - Set CTS blocked flag
 *
 * If CTS notification is enabled:
 *   - Set notification flag and call data receive handler if present
 *
 * Always advances the event count to wake any waiters.
 *
 * Original address: 0x00e1c6da
 */

#include "sio/sio_internal.h"

/* Accessor macros for byte-level access */
#define SIO_DESC_CTRL(desc)        (*(uint8_t *)((char *)(desc) + 0x53))
#define SIO_DESC_INT_NOTIFY(desc)  (*(uint8_t *)((char *)(desc) + 0x57))
#define SIO_DESC_STATUS(desc)      (*(uint8_t *)((char *)(desc) + 0x67))
#define SIO_DESC_XMIT_STATE(desc)  (*(uint8_t *)((char *)(desc) + 0x75))

void SIO_$I_CTS_CHANGE(sio_desc_t *desc, int8_t cts_state)
{
    if (cts_state < 0) {
        /*
         * CTS asserted - clear blocked flag and restart transmit
         */
        SIO_DESC_XMIT_STATE(desc) &= ~SIO_XMIT_CTS_BLOCKED;
        SIO_$I_TSTART(desc);
    } else {
        /*
         * CTS deasserted - if CTS flow control enabled, block transmit
         */
        if ((SIO_DESC_CTRL(desc) & SIO_CTRL_CTS_FLOW) != 0) {
            SIO_DESC_XMIT_STATE(desc) |= SIO_XMIT_CTS_BLOCKED;
        }
    }

    /*
     * If CTS change notification is enabled, set status flag
     * and call data receive handler if present
     */
    if ((SIO_DESC_INT_NOTIFY(desc) & SIO_INT_CTS_CHANGE) != 0) {
        SIO_DESC_STATUS(desc) |= SIO_STAT_CTS_NOTIFY;

        if (desc->data_rcv != 0) {
            ((void (*)(uint32_t, uint16_t))desc->data_rcv)(desc->owner, 0);
        }
    }

    /* Advance event count to wake any waiters */
    EC_$ADVANCE_WITHOUT_DISPATCH(&desc->ec);
}
