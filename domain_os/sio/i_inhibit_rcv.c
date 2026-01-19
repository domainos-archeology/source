/*
 * SIO_$I_INHIBIT_RCV - Control receive inhibit state
 *
 * Controls software flow control (XON/XOFF) for receive operations.
 * When inhibited, sends XOFF to the remote end. When released,
 * sends XON.
 *
 * If update_xmit is set, also updates the transmit state machine
 * to handle deferred transmit inhibit operations.
 *
 * Original address: 0x00e1c94a
 */

#include "sio/sio_internal.h"

/* Accessor macros for byte-level access */
#define SIO_DESC_CTRL(desc)        (*(uint8_t *)((char *)(desc) + 0x53))
#define SIO_DESC_FLOW_CTRL(desc)   (*(uint8_t *)((char *)(desc) + 0x4f))
#define SIO_DESC_XMIT_STATE(desc)  (*(uint8_t *)((char *)(desc) + 0x75))

void SIO_$I_INHIBIT_RCV(sio_desc_t *desc, int8_t inhibit, int8_t update_xmit)
{
    status_$t status;

    /*
     * Check if software flow control is enabled
     */
    if ((SIO_DESC_CTRL(desc) & SIO_CTRL_SOFT_FLOW) != 0) {
        if (inhibit < 0) {
            /* Inhibit - clear flow control bit (send XOFF) */
            SIO_DESC_FLOW_CTRL(desc) &= ~0x01;
        } else {
            /* Release - set flow control bit (send XON) */
            SIO_DESC_FLOW_CTRL(desc) |= 0x01;
        }

        /*
         * Call set_params to update hardware with new flow state
         * Parameter mask 0x20 = software flow control setting
         */
        ((void (*)(uint32_t, void *, uint32_t, status_$t *))desc->set_params)(
            desc->context,
            &desc->params,
            0x20,
            &status
        );
    }

    /*
     * If update_xmit is set, handle deferred transmit state
     */
    if (update_xmit < 0) {
        if (inhibit < 0) {
            /*
             * Inhibiting receive - clear deferred inhibit flag
             * If transmit not currently deferred complete, set defer pending
             */
            SIO_DESC_XMIT_STATE(desc) &= ~SIO_XMIT_DEFER_INHIBIT;

            if ((SIO_DESC_XMIT_STATE(desc) & SIO_XMIT_DEFER_COMPLETE) == 0) {
                SIO_DESC_XMIT_STATE(desc) |= SIO_XMIT_DEFER_PENDING;
            }
        } else {
            /*
             * Releasing inhibit - if transmit is deferred complete,
             * set deferred inhibit flag
             */
            if ((SIO_DESC_XMIT_STATE(desc) & SIO_XMIT_DEFER_COMPLETE) != 0) {
                SIO_DESC_XMIT_STATE(desc) |= SIO_XMIT_DEFER_INHIBIT;
            }
            SIO_DESC_XMIT_STATE(desc) &= ~SIO_XMIT_DEFER_PENDING;
        }

        /* Try to restart transmission */
        SIO_$I_TSTART(desc);
    }
}
