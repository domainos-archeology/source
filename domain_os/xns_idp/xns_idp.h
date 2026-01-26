/*
 * xns_idp/xns_idp.h - XNS IDP Protocol Functions
 *
 * This header provides the XNS IDP protocol API.
 * The actual functions are declared in xns/xns.h, this is a
 * convenience header for backward compatibility.
 */

#ifndef XNS_IDP_H
#define XNS_IDP_H

#include "xns/xns.h"

/*
 * Note: All XNS_IDP_$* functions are declared in xns/xns.h
 *
 * Key functions available:
 *   - XNS_IDP_$INIT           - Initialize IDP subsystem
 *   - XNS_IDP_$OPEN           - Open a channel (user-level)
 *   - XNS_IDP_$CLOSE          - Close a channel (user-level)
 *   - XNS_IDP_$SEND           - Send a packet (user-level)
 *   - XNS_IDP_$RECEIVE        - Receive a packet (user-level)
 *   - XNS_IDP_$OS_OPEN        - Open a channel (OS-level)
 *   - XNS_IDP_$OS_CLOSE       - Close a channel (OS-level)
 *   - XNS_IDP_$OS_SEND        - Send a packet (OS-level)
 *   - XNS_IDP_$OS_ADD_PORT    - Add port to channel
 *   - XNS_IDP_$OS_DELETE_PORT - Remove port from channel
 *   - XNS_IDP_$CHECKSUM       - Calculate IDP checksum
 */

#endif /* XNS_IDP_H */
