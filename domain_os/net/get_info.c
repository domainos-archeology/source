/*
 * NET_$GET_INFO - Get network information
 *
 * Currently unimplemented - returns operation not defined error.
 *
 * Original address: 0x00E5A192
 * Original size: 18 bytes
 */

#include "net/net_internal.h"

void NET_$GET_INFO(void *net_id, void *port, void *param3, void *param4,
                   status_$t *status_ret)
{
    (void)net_id;
    (void)port;
    (void)param3;
    (void)param4;

    *status_ret = status_$network_operation_not_defined_on_hardware;
}
