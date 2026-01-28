/*
 * NET_$IOCTL - Network I/O control
 *
 * Looks up the appropriate device handler and calls its IOCTL routine.
 *
 * Original address: 0x00E5A270
 * Original size: 92 bytes
 */

#include "net/net_internal.h"

/*
 * Device-specific IOCTL handler type
 */
typedef void (*net_ioctl_handler_t)(int16_t *port, void *param3,
                                     status_$t *status_ret);

void NET_$IOCTL(int16_t *net_id, int16_t *port, void *param3, void *param4,
                void *param5, status_$t *status_ret)
{
#if defined(ARCH_M68K)
    net_handler_t handler;

    (void)param4;
    (void)param5;

    /*
     * Find the IOCTL handler for this network/port.
     */
    handler = NET_$FIND_HANDLER(*net_id, *port, NET_HANDLER_OFF_IOCTL, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Call the device-specific IOCTL handler.
     */
    ((net_ioctl_handler_t)handler)(port, param3, status_ret);

#else
    (void)net_id;
    (void)port;
    (void)param3;
    (void)param4;
    (void)param5;
    *status_ret = status_$network_operation_not_defined_on_hardware;
#endif
}
