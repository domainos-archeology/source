/*
 * NET_$OPEN - Open a network connection
 *
 * Looks up the appropriate device handler and calls its OPEN routine.
 * On success, registers a cleanup handler (type 10) for the process.
 *
 * Original address: 0x00E5A1A4
 * Original size: 112 bytes
 */

#include "net/net_internal.h"

/*
 * Device-specific OPEN handler type
 */
typedef void (*net_open_handler_t)(int16_t *port, void *param3, int16_t param4,
                                    int16_t param5, status_$t *status_ret);

void NET_$OPEN(int16_t *net_id, int16_t *port, void *param3, void *param4,
               void *param5, status_$t *status_ret)
{
#if defined(M68K)
    net_handler_t handler;

    /*
     * Find the OPEN handler for this network/port.
     */
    handler = NET_$FIND_HANDLER(*net_id, *port, NET_HANDLER_OFF_OPEN, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Call the device-specific OPEN handler.
     * Parameters are passed through to the device handler.
     */
    ((net_open_handler_t)handler)(port, param3,
                                   (int16_t)((uint32_t)param5 >> 16),
                                   (int16_t)((uint32_t)status_ret >> 16),
                                   status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Register process cleanup handler (type 10 = NET cleanup).
     */
    PROC2_$SET_CLEANUP(10);

#else
    (void)net_id;
    (void)port;
    (void)param3;
    (void)param4;
    (void)param5;
    *status_ret = status_$network_operation_not_defined_on_hardware;
#endif
}
