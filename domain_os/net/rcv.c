/*
 * NET_$RCV - Receive data from network
 *
 * Looks up the driver's svc_read entry and calls it.
 *
 * Original address: 0x00E5A2CC
 * Original size: 104 bytes
 */

#include "net/net_internal.h"

void NET_$RCV(int16_t *net_id, int16_t *port, uint32_t param3,
              int16_t *param4, uint32_t param5, uint32_t param6,
              int16_t *param7, uint32_t param8, status_$t *status_ret)
{
    net_io_$driver_fn_t handler;

    /* 0x00E5A2D4-0x00E5A2FC */
    handler = NET_$FIND_HANDLER(*net_id, (uint16_t)*port,
                                NET_HANDLER_OFF_RCV, status_ret);

    /* 0x00E5A304 tst.l (A3) */
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E5A308-0x00E5A328: eight arguments and NO result slot; param4 and
     * param7 are each dereferenced to one word.
     */
    ((net_$svc_xfer_fn_t)handler)(port, param3, *param4, param5, param6,
                                  *param7, param8, status_ret);
}
