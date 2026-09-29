/*
 * MAC - Media Access Control Module - Internal Header
 *
 * Internal definitions for the MAC subsystem.
 */

#ifndef MAC_INTERNAL_H
#define MAC_INTERNAL_H

#include "ec/ec.h"
#include "fim/fim.h"
#include "mac/mac.h"
#include "mac_os/mac_os.h"   /* MAC_OS_$* lower-level operations */
#include "ml/ml.h"
#include "netbuf/netbuf.h"
#include "os/os.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "route/route.h"
#include "sock/sock.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* Port info table entry size */
#define MAC_PORT_INFO_SIZE 0x5C

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * mac_$copy_to_buffers (0x00E0BD2C) is a nested Pascal procedure of
 * MAC_$RECEIVE and reaches its parent's frame through a static link, so it is
 * emitted as a file-static function inside mac/receive.c rather than declared
 * here.
 */

/*
 * ============================================================================
 * Global Data References
 * ============================================================================
 */

/*
 * source-702z removed three unused absolute-address spellings and their
 * host stand-ins: the exclusion lock at MAC_OS + 0x868 (0xE231F8), the port
 * table at 0xE2E0A0 (ROUTE_$PORT_ARRAY, route/route.h) and the socket
 * pointer array at 0xE28DB0 (the SOCK_$DATA block, sock/sock.h).
 */

#endif /* MAC_INTERNAL_H */
