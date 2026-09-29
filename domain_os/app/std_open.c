/*
 * APP_$STD_OPEN - open the standard application-protocol IDP channel
 *
 * Initialises the APP exclusion lock and opens socket 0x0499 on every port
 * that carries the first ROUTE port's network, with APP_$DEMUX as the
 * channel's demux vector.
 *
 * Original address: 0x00E00B92
 */

#include "app/app_internal.h"

void APP_$STD_OPEN(void)
{
    status_$t status;                   /* A6-0x2C */
    xns_$os_open_opt_t opt;             /* A6-0x28 */

    ML_$EXCLUSION_INIT(&APP_$DATA.exclusion_lock);           /* 0x00E00BA0 */

    /*
     * 0x00E00BA8 "move.l #0x4990002,(-0x28,A6)" writes both halves of the
     * record's first longword: socket 0x0499 and the flag word 0x0002, whose
     * low byte is XNS_OPEN_FLAG_BIND_LOCAL.
     */
    opt.socket = XNS_SOCKET_ROUTER;
    opt.flags_channel = XNS_OPEN_FLAG_BIND_LOCAL;

    /*
     * 0x00E00BB0 "movea.l (0x00e26ee8).l,A0 / move.l (A0),(-0x20,A6)":
     * ROUTE_$PORTP[0] is followed and its FIRST longword - route_$port_t's
     * network number - becomes the network to bind to.
     */
    opt.network = ROUTE_$WIRED_DATA.portp[0]->network;

    opt.demux = (uint32_t)(uintptr_t)&APP_$DEMUX;       /* 0x00E00BBA */

    XNS_IDP_$OS_OPEN(&opt, &status);                    /* 0x00E00BCA */

    if (status == status_$ok) {                         /* 0x00E00BD2 */
        /* 0x00E00BD8 "move.w (-0x26,A6),(0x14,A5)": the record's +0x02 now
         * holds the channel index. */
        APP_$DATA.std_idp_channel = opt.flags_channel;
    }
}
