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

/*
 * NET_IO_$ALL_F_ADDR (0xE244F0, A5+0 in NET_IO_$SEND) - the broadcast node
 * mask; image value 0x000FFFFF.  A node is "all ones" when
 * (node & 0xFFFFFF & NET_IO_$ALL_F_ADDR) == NET_IO_$ALL_F_ADDR.
 */
extern uint32_t NET_IO_$ALL_F_ADDR;

/*
 * net_io_$put_in_sock_common (0x00E0E238, 610 bytes, was FUN_00e0e238; the
 * NET_IO code segment's first routine, no map symbol) - deliver a packet to
 * a socket of this node (net_io/put_in_sock_common.c).  Called by
 * NET_IO_$PUT_IN_SOCK (0x00E0E4CE) and NET_IO_$PUT_IN_SOCK_INT (0x00E0E508)
 * with port = 0 (it then finds the port itself) and by NET_IO_$SEND's
 * loopback paths (0x00E0E756, 0x00E0E80E) with the port record.  A Pascal
 * function: true (0xFF) in D0 when a socket queued the packet, false when it
 * was dropped and its buffers returned.  Frame offsets: port 0x08,
 * port_type 0x0C, socket 0x0E, int_level byte 0x10, hdr_va_p 0x12,
 * data_pa_p 0x16, hdr_len 0x1A, data_len 0x1C, ec_ret 0x1E (handed to
 * SOCK_$PUT_INT).  Runs on its caller's A5 = the NET_IO segment.
 */
int8_t net_io_$put_in_sock_common(route_$port_t *port, uint16_t port_type,
                                  uint16_t socket, int8_t int_level,
                                  uint32_t *hdr_va_p, uint32_t *data_pa_p,
                                  uint16_t hdr_len, uint16_t data_len,
                                  ec_$eventcount_t **ec_ret);

/*
 * The transmit procedure variable net_io_$driver_t.sendp (+0x08) as
 * NET_IO_$SEND calls it (0x00E0E86A-0x00E0E896): RING_$SENDP's shape, with
 * the header passed as the VA the caller holds.
 */
typedef void (*net_io_$sendp_fn_t)(uint16_t *socket, uint32_t hdr_pa,
                                   void *hdr, uint16_t hdr_len,
                                   const uint32_t *data_pages,
                                   uint32_t data_va, uint16_t data_len,
                                   const uint16_t *flags,
                                   uint16_t *xmit_status,
                                   status_$t *status_ret);

#endif /* NET_IO_NET_IO_INTERNAL_H */
