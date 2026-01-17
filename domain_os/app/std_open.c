/*
 * APP_$STD_OPEN - Open standard application channel
 *
 * Opens the standard application protocol channel via XNS IDP.
 * Initializes the exclusion lock and registers APP_$DEMUX as
 * the packet handler for protocol 0x0499.
 *
 * Original address: 0x00E00B92
 */

#include "app/app_internal.h"

void APP_$STD_OPEN(void)
{
    status_$t status;
    xns_idp_open_params_t params;
    uint16_t channel;

    /* Initialize the exclusion lock for APP operations */
    ML_$EXCLUSION_INIT(&APP_$EXCLUSION_LOCK);

    /* Set up XNS IDP open parameters */
    params.protocol = 0x04990002;  /* Protocol 0x0499, flags 0x0002 */
    params.demux_handler = (void *)APP_$DEMUX;
    params.net_info = *(uint32_t *)ROUTE_$PORTP;

    /* Open the XNS IDP channel */
    XNS_IDP_$OS_OPEN(&params, &status);

    /* If successful, save the channel number */
    if (status == status_$ok) {
        /* Channel is at offset 2 in params (the low 16 bits of protocol) */
        channel = *(uint16_t *)((uint8_t *)&params + 2);
        APP_$STD_IDP_CHANNEL = channel;
    }
}
