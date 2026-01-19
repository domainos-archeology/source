/*
 * SIO_$I_INHIBIT_XMIT - Control transmit inhibit state
 *
 * Controls software flow control for transmit operations.
 * When the remote sends XOFF, transmission is inhibited.
 * When XON is received, transmission resumes.
 *
 * Original address: 0x00e1c9ce
 */

#include "sio/sio_internal.h"

/* Accessor macro for transmit state byte */
#define SIO_DESC_XMIT_STATE(desc)  (*(uint8_t *)((char *)(desc) + 0x75))

int8_t SIO_$I_INHIBIT_XMIT(sio_desc_t *desc, int8_t inhibit)
{
    if (inhibit < 0) {
        /* Inhibit transmit (XOFF received) */
        SIO_DESC_XMIT_STATE(desc) |= SIO_XMIT_INHIBITED;
    } else {
        /* Release inhibit (XON received) */
        SIO_DESC_XMIT_STATE(desc) &= ~SIO_XMIT_INHIBITED;

        /* Try to restart transmission */
        SIO_$I_TSTART(desc);
    }

    return inhibit;
}
