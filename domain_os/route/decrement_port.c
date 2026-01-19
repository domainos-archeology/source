/*
 * ROUTE_$DECREMENT_PORT - Decrement port counters during port close
 *
 * This helper function is called when closing a routing port. It calls
 * RIP_$PORT_CLOSE to invalidate routes through the port, then decrements
 * the appropriate port counter. If only one port remains active, it halts
 * the router. If routing was active and both counters drop below 2, it
 * advances the control event counter to signal the routing process.
 *
 * Original address: 0x00E69E40
 * Size: 130 bytes
 */

#include "route/route_internal.h"
#include "rip/rip.h"
#include "ec/ec.h"

/*
 * ROUTE_$DECREMENT_PORT - Decrement routing port counter and cleanup
 *
 * Called during port close to decrement the appropriate routing port
 * counter (STD or normal) and potentially halt the router.
 *
 * @param delete_flag      Delete notification flag for RIP_$PORT_CLOSE
 * @param port_index       Port index being closed (0-7)
 * @param port_type_flag   Port type: negative = STD port, non-negative = normal
 *
 * Original address: 0x00E69E40
 */
void ROUTE_$DECREMENT_PORT(int8_t delete_flag, int16_t port_index, int8_t port_type_flag)
{
    /* First, notify RIP to invalidate routes through this port */
    RIP_$PORT_CLOSE(port_index, port_type_flag, delete_flag);

    if (port_type_flag < 0) {
        /* STD port - decrement STD counter */
        ROUTE_$STD_N_ROUTING_PORTS--;

        if (ROUTE_$STD_N_ROUTING_PORTS == 1) {
            /*
             * Only one STD port left - halt the router.
             * Pass 0xFFFF to indicate STD port halt.
             */
            RIP_$HALT_ROUTER(0xFFFF);
        }
    } else {
        /* Normal port - decrement normal counter */
        ROUTE_$N_ROUTING_PORTS--;

        if (ROUTE_$N_ROUTING_PORTS == 1) {
            /*
             * Only one normal port left - halt the router.
             * Pass 0 to indicate normal port halt.
             */
            RIP_$HALT_ROUTER(0);
        }
    }

    /*
     * Check if routing should signal completion.
     *
     * If ROUTE_$ROUTING has the high bit of its high byte set (routing was active)
     * and both port counters are now < 2 (meaning at most 1 port each),
     * advance the control EC to signal the routing process.
     *
     * Note: Original tests just the high byte (big-endian), checking bit 7.
     */
    if ((int8_t)(ROUTE_$ROUTING >> 8) < 0 &&
        ROUTE_$N_ROUTING_PORTS < 2 &&
        ROUTE_$STD_N_ROUTING_PORTS < 2) {
        EC_$ADVANCE((ec_$eventcount_t *)&ROUTE_$CONTROL_EC);
    }
}
