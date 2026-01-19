/*
 * SIO_$I_XMIT_DONE - Transmit complete interrupt handler
 *
 * Called when a character transmission completes. Clears the
 * transmit-active flag and calls SIO_$I_TSTART to send the next
 * character if one is available.
 *
 * Original address: 0x00e1c6b4
 */

#include "sio/sio_internal.h"

/* Accessor for transmit state byte at offset 0x75 */
#define SIO_DESC_XMIT_STATE(desc)   (*(uint8_t *)((char *)(desc) + 0x75))

int8_t SIO_$I_XMIT_DONE(sio_desc_t *desc)
{
    /* Clear transmit-active flag (bit 0) */
    SIO_DESC_XMIT_STATE(desc) &= ~SIO_XMIT_ACTIVE;

    /* Try to start next transmission */
    SIO_$I_TSTART(desc);

    /* Return non-zero if transmit is now active */
    return -((SIO_DESC_XMIT_STATE(desc) & SIO_XMIT_ACTIVE) != 0);
}
