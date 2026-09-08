/*
 * NET_$ - Network Device Abstraction Layer
 *
 * Provides a unified interface to different network hardware types
 * (Ethernet, Token Ring, etc.) through a dispatch mechanism.
 * Operations are routed to device-specific handlers based on
 * network ID and port number.
 *
 * The five dispatchers below (NET_$OPEN, NET_$CLOSE, NET_$IOCTL, NET_$SEND,
 * NET_$RCV) all share one shape: NET_$FIND_HANDLER turns (net_id, port, slot
 * offset) into a driver entry point, and on success the entry point is called
 * with the dispatcher's own arguments minus net_id.  The slots live in the
 * net_io_$driver_t record route_$port_t.driver_info points at.
 */

#ifndef NET_NET_H
#define NET_NET_H

#include "os/os.h"
#include "network/network.h"   /* status_$network_* (module 0x11) */
#include "net_io/net_io.h"     /* net_io_$driver_t: the handler slot record */

/*
 * Handler slot offsets within net_io_$driver_t.
 *
 * The image does not use literal constants here: each dispatcher recomputes
 * its own slot offset at run time from the absolute address of that slot in
 * NET_IO_$NIL_DRIVER (0xE244F4), e.g. NET_$OPEN at 0x00E5A1B8:
 *
 *   00e5a1b8  lea (0xe2451c).l,A0        ; &NET_IO_$NIL_DRIVER[0].svc_open
 *   00e5a1be  move.l A0,D0
 *   00e5a1c0  sub.l #0xe244f4,D0         ; - &NET_IO_$NIL_DRIVER[0]
 *   00e5a1c6  move.w D0w,-(SP)           ; = 0x28
 *
 * The constants below are the same thing said with the typed record, so that
 * the arithmetic follows the record on a host whose procedure variables are
 * wider than the target's.  The static assertions tie them back to the
 * differences the image computes; net_io_$driver_t only has the image layout
 * on a 32-bit-pointer target, so they are m68k-only exactly as net_io.h's own
 * layout checks are.
 */
#define NET_HANDLER_OFF_OPEN    ((uint16_t)offsetof(net_io_$driver_t, svc_open))
#define NET_HANDLER_OFF_CLOSE   ((uint16_t)offsetof(net_io_$driver_t, svc_close))
#define NET_HANDLER_OFF_IOCTL   ((uint16_t)offsetof(net_io_$driver_t, svc_ioctl))
#define NET_HANDLER_OFF_SEND    ((uint16_t)offsetof(net_io_$driver_t, svc_write))
#define NET_HANDLER_OFF_RCV     ((uint16_t)offsetof(net_io_$driver_t, svc_read))

#if defined(ARCH_M68K)
_Static_assert(NET_HANDLER_OFF_OPEN  == (0x00E2451CUL - 0x00E244F4UL),
               "NET_HANDLER_OFF_OPEN");   /* 0x28, 0x00E5A1B8 */
_Static_assert(NET_HANDLER_OFF_CLOSE == (0x00E24520UL - 0x00E244F4UL),
               "NET_HANDLER_OFF_CLOSE");  /* 0x2C, 0x00E5A228 */
_Static_assert(NET_HANDLER_OFF_IOCTL == (0x00E24524UL - 0x00E244F4UL),
               "NET_HANDLER_OFF_IOCTL");  /* 0x30, 0x00E5A284 */
_Static_assert(NET_HANDLER_OFF_SEND  == (0x00E24528UL - 0x00E244F4UL),
               "NET_HANDLER_OFF_SEND");   /* 0x34, 0x00E5A348 */
_Static_assert(NET_HANDLER_OFF_RCV   == (0x00E2452CUL - 0x00E244F4UL),
               "NET_HANDLER_OFF_RCV");    /* 0x38, 0x00E5A2E0 */
#endif

/*
 * Driver entry point shapes
 *
 * net_io_$driver_t stores all sixteen slots as one generic procedure variable
 * because the argument lists differ; each dispatcher casts its slot to the
 * shape it pushes.  The three shapes NET_$ uses are below.  In each case the
 * longword arguments are opaque: the dispatchers copy them straight through
 * from their own frames without reading them, so they are spelled uint32_t
 * (an m68k-width cell) rather than being given an invented pointer type.
 */

/*
 * net_$svc_ctl_fn_t - the shape of svc_open / svc_close / svc_ioctl
 *
 * NET_$OPEN pushes, right to left (0x00E5A1E0..0x00E5A1F4):
 *   subq.l #0x2,SP            ; word result slot; the result is discarded
 *   pea (A3)                  ; arg 5: status_ret
 *   move.l (0x18,A6),-(SP)    ; arg 4: param5, by value
 *   movea.l (0x14,A6),A1
 *   move.w (A1),-(SP)         ; arg 3: *param4, one word by value
 *   move.l (0x10,A6),-(SP)    ; arg 2: param3, by value
 *   pea (A2)                  ; arg 1: port, by reference
 *   jsr (A0)
 *   lea (0x14,SP),SP          ; 0x12 of arguments plus the 2-byte slot
 * NET_$CLOSE (0x00E5A250) and NET_$IOCTL (0x00E5A2AC) push the identical
 * five arguments and the identical result slot; they simply have no code
 * after the jsr, so unlk does the popping.
 */
typedef int16_t (*net_$svc_ctl_fn_t)(int16_t *port, uint32_t param3,
                                     int16_t param4, uint32_t param5,
                                     status_$t *status_ret);

/*
 * net_$svc_xfer_fn_t - the shape of svc_write / svc_read
 *
 * NET_$RCV pushes, right to left (0x00E5A308..0x00E5A328) - note there is no
 * result slot here, unlike the control entries above:
 *   pea (A3)                  ; arg 8: status_ret
 *   move.l (0x24,A6),-(SP)    ; arg 7: param8
 *   movea.l (0x20,A6),A1
 *   move.w (A1),-(SP)         ; arg 6: *param7, one word
 *   move.l (0x1c,A6),-(SP)    ; arg 5: param6
 *   move.l (0x18,A6),-(SP)    ; arg 4: param5
 *   movea.l (0x14,A6),A4
 *   move.w (A4),-(SP)         ; arg 3: *param4, one word
 *   move.l (0x10,A6),-(SP)    ; arg 2: param3
 *   pea (A2)                  ; arg 1: port
 *   jsr (A0)
 * NET_$SEND (0x00E5A370..0x00E5A390) is instruction-for-instruction the same.
 */
typedef void (*net_$svc_xfer_fn_t)(int16_t *port, uint32_t param3,
                                   int16_t param4, uint32_t param5,
                                   uint32_t param6, int16_t param7,
                                   uint32_t param8, status_$t *status_ret);

/*
 * Public API Functions
 */

/*
 * NET_$GET_INFO - Get network information
 *
 * Currently returns status_$network_operation_not_defined_on_hardware
 * as this operation is not implemented.
 */
void NET_$GET_INFO(void *net_id, void *port, void *param3, void *param4,
                   status_$t *status_ret);

/*
 * NET_$OPEN - Open a network connection
 *
 * Looks up the driver's svc_open entry and calls it.  Registers cleanup
 * handler (bit 10) on success.
 *
 * Parameters:
 *   net_id     - Network identifier, by reference (one word is read)
 *   port       - Port number, by reference; passed on to the driver
 *   param3     - Opaque longword, passed on by value
 *   param4     - By reference; one word is read and passed on by value
 *   param5     - Opaque longword, passed on by value
 *   status_ret - Status return
 */
void NET_$OPEN(int16_t *net_id, int16_t *port, uint32_t param3,
               int16_t *param4, uint32_t param5, status_$t *status_ret);

/*
 * NET_$CLOSE - Close a network connection
 *
 * Looks up the driver's svc_close entry and calls it with the same five
 * arguments NET_$OPEN passes.
 */
void NET_$CLOSE(int16_t *net_id, int16_t *port, uint32_t param3,
                int16_t *param4, uint32_t param5, status_$t *status_ret);

/*
 * NET_$IOCTL - Network I/O control
 *
 * Looks up the driver's svc_ioctl entry and calls it with the same five
 * arguments NET_$OPEN passes.
 */
void NET_$IOCTL(int16_t *net_id, int16_t *port, uint32_t param3,
                int16_t *param4, uint32_t param5, status_$t *status_ret);

/*
 * NET_$SEND - Send data on network
 *
 * Looks up the driver's svc_write entry and calls it.
 */
void NET_$SEND(int16_t *net_id, int16_t *port, uint32_t param3,
               int16_t *param4, uint32_t param5, uint32_t param6,
               int16_t *param7, uint32_t param8, status_$t *status_ret);

/*
 * NET_$RCV - Receive data from network
 *
 * Looks up the driver's svc_read entry and calls it.
 */
void NET_$RCV(int16_t *net_id, int16_t *port, uint32_t param3,
              int16_t *param4, uint32_t param5, uint32_t param6,
              int16_t *param7, uint32_t param8, status_$t *status_ret);

#endif /* NET_NET_H */
