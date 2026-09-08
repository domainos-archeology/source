/*
 * NET_$ Internal Definitions
 *
 * Internal data structures and helper functions for the NET subsystem.
 */

#ifndef NET_NET_INTERNAL_H
#define NET_NET_INTERNAL_H

#include "net/net.h"
#include "net_io/net_io.h"     /* net_io_$driver_t, net_io_$driver_fn_t */
#include "route/route.h"       /* route_$port_t, ROUTE_$FIND_PORTP */
#include "proc2/proc2.h"

/*
 * NET_$FIND_HANDLER - Find device handler for network operation
 *
 * Locates the port with ROUTE_$FIND_PORTP and fetches one slot out of the
 * net_io_$driver_t record route_$port_t.driver_info (+0x48) points at.
 *
 * Parameters:
 *   net_id      - Network identifier
 *   port        - Port number
 *   handler_off - Byte offset of the slot within net_io_$driver_t
 *                 (NET_HANDLER_OFF_*)
 *   status_ret  - Status return
 *
 * Returns:
 *   The driver entry point, or NULL on error.  The caller casts it to the
 *   shape it is about to push (net_$svc_ctl_fn_t / net_$svc_xfer_fn_t).
 *   Sets status to:
 *   - status_$internet_unknown_network_port if the port does not exist
 *   - status_$network_operation_not_defined_on_hardware if the slot is nil
 *   - status_$ok on success
 *
 * The image returns the entry point in A0 and never writes the two-byte
 * function-result slot its callers reserve (0x00E5A1D4 pops 0xC bytes for
 * 0xA bytes of arguments), so the slot is left out of the C signature.
 *
 * Original address: 0x00E5A128
 * Original size: 106 bytes
 */
net_io_$driver_fn_t NET_$FIND_HANDLER(int16_t net_id, uint16_t port,
                                      uint16_t handler_off,
                                      status_$t *status_ret);

#endif /* NET_NET_INTERNAL_H */
