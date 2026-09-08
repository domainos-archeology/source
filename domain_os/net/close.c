/*
 * NET_$CLOSE - Close a network connection
 *
 * Looks up the driver's svc_close entry and calls it.
 *
 * Original address: 0x00E5A214
 * Original size: 92 bytes
 */

#include "net/net_internal.h"

void NET_$CLOSE(int16_t *net_id, int16_t *port, uint32_t param3,
                int16_t *param4, uint32_t param5, status_$t *status_ret)
{
    net_io_$driver_fn_t handler;

    /* 0x00E5A21C-0x00E5A244 */
    handler = NET_$FIND_HANDLER(*net_id, (uint16_t)*port,
                                NET_HANDLER_OFF_CLOSE, status_ret);

    /* 0x00E5A24C tst.l (A3) */
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E5A250-0x00E5A264: the same five arguments and discarded word
     * result slot NET_$OPEN pushes.  The jsr is the last instruction before
     * the epilogue, so the image never pops the arguments - unlk does it.
     */
    (void)((net_$svc_ctl_fn_t)handler)(port, param3, *param4, param5,
                                       status_ret);
}
