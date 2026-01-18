/*
 * NET_$FIND_HANDLER - Find device handler for network operation
 *
 * Looks up the appropriate handler function for the given network/port
 * combination and operation offset.
 *
 * Original address: 0x00E5A128
 * Original size: 106 bytes
 */

#include "net/net_internal.h"

net_handler_t NET_$FIND_HANDLER(int16_t net_id, uint16_t port,
                                 uint16_t handler_off, status_$t *status_ret)
{
#if defined(M68K)
    void *port_ptr;
    void *handler_table;
    net_handler_t handler;

    /*
     * Find the port structure using ROUTE_$FIND_PORTP.
     * Returns NULL if port not found.
     */
    port_ptr = ROUTE_$FIND_PORTP(net_id, (uint32_t)port);

    if (port_ptr == NULL) {
        *status_ret = status_$internet_unknown_network_port;
        return NULL;
    }

    /*
     * Get the handler table from port structure (offset 0x48).
     * Then index into it using the handler offset.
     */
    handler_table = *(void **)((uint8_t *)port_ptr + PORT_OFF_HANDLER_TABLE);
    handler = *(net_handler_t *)((uint8_t *)handler_table + handler_off);

    if (handler == NULL) {
        *status_ret = status_$network_operation_not_defined_on_hardware;
        return NULL;
    }

    *status_ret = status_$ok;
    return handler;

#else
    (void)net_id;
    (void)port;
    (void)handler_off;
    *status_ret = status_$network_operation_not_defined_on_hardware;
    return NULL;
#endif
}
