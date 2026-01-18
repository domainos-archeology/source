/*
 * NET_$ Internal Definitions
 *
 * Internal data structures and helper functions for the NET subsystem.
 */

#ifndef NET_NET_INTERNAL_H
#define NET_NET_INTERNAL_H

#include "net/net.h"
#include "route/route.h"
#include "proc2/proc2.h"

/*
 * Handler table base address
 * Function pointer addresses are stored relative to this base.
 */
#define NET_HANDLER_TABLE_BASE  0xE244F4

/*
 * Port structure layout (partial)
 *
 * The port structure contains at offset 0x48 a pointer to
 * a handler table for device-specific operations.
 */
#define PORT_OFF_HANDLER_TABLE  0x48

/*
 * Handler function pointer type
 */
typedef void (*net_handler_t)(void);

/*
 * NET_$FIND_HANDLER - Find device handler for network operation
 *
 * Looks up the appropriate handler function for the given network/port
 * combination and operation offset.
 *
 * Parameters:
 *   net_id      - Network identifier
 *   port        - Port number
 *   handler_off - Offset into handler table (NET_HANDLER_OFF_*)
 *   status_ret  - Status return
 *
 * Returns:
 *   Function pointer to handler, or NULL on error.
 *   Sets status to:
 *   - status_$internet_unknown_network_port if port not found
 *   - status_$network_operation_not_defined_on_hardware if handler is NULL
 *   - status_$ok on success
 *
 * Original address: 0x00E5A128
 * Original size: 106 bytes
 */
net_handler_t NET_$FIND_HANDLER(int16_t net_id, uint16_t port,
                                 uint16_t handler_off, status_$t *status_ret);

#endif /* NET_NET_INTERNAL_H */
