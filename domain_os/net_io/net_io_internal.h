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
#include "route/route.h"        /* route_$port_t, ROUTE_$PORTP, ROUTE_$FIND_PORTP,
                                 * route_$user_stat_t, ROUTE_$USER_STAT */
#include "sock/sock.h"          /* SOCK_$ALLOCATE, SOCK_$DATA */
#include "time/time.h"          /* TIME_$CURRENT_CLOCKH */

/*
 * PROC2_$SET_CLEANUP handler class NET_IO_$CREATE_PORT and NET_$OPEN both
 * register ("move.w #0xa,-(SP)" at 0x00E5A692 and 0x00E5A200).
 */
#define NET_IO_$CLEANUP_CLASS   10

#endif /* NET_IO_NET_IO_INTERNAL_H */
