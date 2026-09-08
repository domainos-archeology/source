/*
 * NET_$FIND_HANDLER - Find device handler for network operation
 *
 * Looks up the port and fetches one procedure-variable slot out of the
 * net_io_$driver_t record the port's driver_info field points at.
 *
 * Original address: 0x00E5A128
 * Original size: 106 bytes
 */

#include "net/net_internal.h"

net_io_$driver_fn_t NET_$FIND_HANDLER(int16_t net_id, uint16_t port,
                                      uint16_t handler_off,
                                      status_$t *status_ret)
{
    route_$port_t *portp;
    net_io_$driver_fn_t *slot;
    net_io_$driver_fn_t handler;

    /*
     * 0x00E5A13E-0x00E5A14E: the port number is widened to a longword
     * before the call ("clr.l D0 / move.w D2w,D0w / move.l D0,-(SP)").
     */
    portp = ROUTE_$FIND_PORTP((uint16_t)net_id, (int32_t)(uint32_t)port);

    /* 0x00E5A150 cmpa.w #0x0,A0 */
    if (portp == NULL) {
        *status_ret = status_$internet_unknown_network_port;   /* 0x002B0003 */
        return NULL;
    }

    /*
     * 0x00E5A15E-0x00E5A16C: the slot address is the driver record plus the
     * caller's offset, with the offset zero-extended to a longword
     * ("andi.l #0xffff,D3 / add.l (0x48,A0),D3").  driver_info is held as a
     * target virtual address, so it is reached with ARCH_VA_TO_PTR.
     */
    slot = (net_io_$driver_fn_t *)((uint8_t *)ARCH_VA_TO_PTR(portp->driver_info)
                                   + (uint32_t)handler_off);

    /* 0x00E5A16E tst.l (A1) - a nil slot means the driver does not do this */
    handler = *slot;
    if (handler == NULL) {
        *status_ret = status_$network_operation_not_defined_on_hardware; /* 0x0011001D */
        return NULL;
    }

    *status_ret = status_$ok;
    return handler;
}
