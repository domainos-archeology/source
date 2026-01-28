/*
 * NET_$SEND - Send data on network
 *
 * Looks up the appropriate device handler and calls its SEND routine.
 *
 * Original address: 0x00E5A334
 * Original size: 104 bytes
 */

#include "net/net_internal.h"

/*
 * Device-specific SEND handler type
 */
typedef void (*net_send_handler_t)(int16_t *port, void *param3, int16_t param4,
                                    void *param5, void *param6, int16_t param7,
                                    void *param8, status_$t *status_ret);

void NET_$SEND(int16_t *net_id, int16_t *port, void *param3, int16_t *param4,
               void *param5, void *param6, int16_t *param7, void *param8,
               status_$t *status_ret)
{
#if defined(ARCH_M68K)
    net_handler_t handler;

    /*
     * Find the SEND handler for this network/port.
     */
    handler = NET_$FIND_HANDLER(*net_id, *port, NET_HANDLER_OFF_SEND, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Call the device-specific SEND handler.
     */
    ((net_send_handler_t)handler)(port, param3, *param4, param5, param6,
                                   *param7, param8, status_ret);

#else
    (void)net_id;
    (void)port;
    (void)param3;
    (void)param4;
    (void)param5;
    (void)param6;
    (void)param7;
    (void)param8;
    *status_ret = status_$network_operation_not_defined_on_hardware;
#endif
}
