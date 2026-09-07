/*
 * NET_IO Internal Definitions
 *
 * Internal types and cross-subsystem dependencies shared by the NET_IO
 * implementation files.  Everything a caller outside net_io/ needs lives in
 * net_io/net_io.h; this header only pulls in what the module's own bodies
 * touch.
 */

#ifndef NET_IO_NET_IO_INTERNAL_H
#define NET_IO_NET_IO_INTERNAL_H

#include "net_io/net_io.h"

#include "ec/ec.h"              /* EC_$INIT, ec_$eventcount_t */
#include "proc1/proc1.h"        /* PROC1_$AS_ID */
#include "proc2/proc2.h"        /* PROC2_$SET_CLEANUP */
#include "route/route.h"        /* route_$port_t, ROUTE_$PORTP, ROUTE_$FIND_PORTP */
#include "sock/sock.h"          /* SOCK_$ALLOCATE, SOCK_$EVENT_COUNTERS */
#include "time/time.h"          /* TIME_$CURRENT_CLOCKH */

/*
 * ROUTE_$USER_STAT lives in route/route_internal.h even though
 * NET_IO_$CREATE_PORT is its allocator and ROUTE_$CLOSE_PORT is the only
 * other accessor.  Reaching across for it keeps a single definition of the
 * record instead of a second one here.
 *
 * TODO(source-tjv5, 0x00E87FD6): route_$user_stat_t and ROUTE_$USER_STAT
 * belong in route/route.h, since their allocator lives outside route/.
 */
#include "route/route_internal.h"

/*
 * PROC2_$SET_CLEANUP handler class NET_IO_$CREATE_PORT and NET_$OPEN both
 * register ("move.w #0xa,-(SP)" at 0x00E5A692 and 0x00E5A200).
 */
#define NET_IO_$CLEANUP_CLASS   10

#endif /* NET_IO_NET_IO_INTERNAL_H */
