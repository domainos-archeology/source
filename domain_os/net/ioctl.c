/*
 * NET_$IOCTL - Network I/O control
 *
 * Looks up the driver's svc_ioctl entry and calls it.
 *
 * Original address: 0x00E5A270
 * Original size: 92 bytes
 */

#include "net/net_internal.h"

void NET_$IOCTL(int16_t *net_id, int16_t *port, uint32_t param3,
                int16_t *param4, uint32_t param5, status_$t *status_ret)
{
    net_io_$driver_fn_t handler;

    /* 0x00E5A278-0x00E5A2A0 */
    handler = NET_$FIND_HANDLER(*net_id, (uint16_t)*port,
                                NET_HANDLER_OFF_IOCTL, status_ret);

    /* 0x00E5A2A8 tst.l (A3) */
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E5A2AC-0x00E5A2C0: the same five arguments and discarded word
     * result slot NET_$OPEN pushes.
     */
    (void)((net_$svc_ctl_fn_t)handler)(port, param3, *param4, param5,
                                       status_ret);
}
