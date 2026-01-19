/*
 * SIO_$I_DCD_CHANGE - DCD signal change handler
 *
 * Called when the Data Carrier Detect signal changes state.
 * If DCD is lost and DCD hangup is enabled, calls the DCD handler.
 *
 * If DCD notification is enabled:
 *   - Set notification flag and call data receive handler if present
 *
 * Always advances the event count to wake any waiters.
 *
 * Original address: 0x00e1c73e
 */

#include "sio/sio_internal.h"

/* Accessor macros for byte-level access */
#define SIO_DESC_CTRL(desc)        (*(uint8_t *)((char *)(desc) + 0x53))
#define SIO_DESC_INT_NOTIFY(desc)  (*(uint8_t *)((char *)(desc) + 0x57))
#define SIO_DESC_STATUS(desc)      (*(uint8_t *)((char *)(desc) + 0x67))

void SIO_$I_DCD_CHANGE(sio_desc_t *desc, int8_t dcd_state)
{
    if (dcd_state >= 0) {
        /*
         * DCD deasserted (carrier lost)
         * If DCD hangup is enabled, call the DCD handler
         */
        if ((SIO_DESC_CTRL(desc) & SIO_CTRL_DCD_HANGUP) != 0) {
            if (desc->dcd_handler != 0) {
                ((void (*)(uint32_t))desc->dcd_handler)(desc->owner);
            }
            goto notify_and_advance;
        }
        /* DCD deasserted but hangup not enabled - check if it's still deasserted */
        if (dcd_state >= 0) {
            goto notify_and_advance;
        }
    }

    /*
     * DCD asserted - try to start transmission
     * (This handles the case where we were waiting for carrier)
     */
    SIO_$I_TSTART(desc);

notify_and_advance:
    /*
     * If DCD change notification is enabled, set status flag
     * and call data receive handler if present
     */
    if ((SIO_DESC_INT_NOTIFY(desc) & SIO_INT_DCD_CHANGE) != 0) {
        SIO_DESC_STATUS(desc) |= SIO_STAT_DCD_NOTIFY;

        if (desc->data_rcv != 0) {
            ((void (*)(uint32_t, uint16_t))desc->data_rcv)(desc->owner, 0);
        }
    }

    /* Advance event count to wake any waiters */
    EC_$ADVANCE_WITHOUT_DISPATCH(&desc->ec);
}
