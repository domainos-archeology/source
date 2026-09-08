/*
 * RIP Miscellaneous Functions
 *
 * Contains utility functions for RIP that don't fit in other categories:
 * - RIP_$ANNOUNCE_NS: Announce name service availability
 * - RIP_$HALT_ROUTER: Gracefully stop the router
 *
 * Original addresses:
 *   RIP_$ANNOUNCE_NS: 0x00E6914E
 *   RIP_$HALT_ROUTER: 0x00E87396
 */

#include "rip/rip_internal.h"
#include "pkt/pkt.h"

#include "rem_name/rem_name.h"   /* REM_NAME_$REGISTER_SERVER */
#include "route/route.h"

/*
 * External data references:
 *   RIP_$BCAST_CONTROL (0xE26EC0), RIP_$NS_ANNOUNCEMENT (0xE26EBE) - rip/rip.h
 *   RIP_$ANNOUNCE_EXTRA (0xE68E28)                                - rip/rip_internal.h
 *   RIP_$HALT_PACKET (0xE87D68), RIP_$HALT_PACKET_DATA (0xE87D78) - route/route.h
 *   ROUTE_$PORT_ARRAY (0xE2E0A0)                                  - route/route.h
 *   NODE_$ME (0xE245A4)                                           - network/network.h
 */

/*
 * RIP_$ANNOUNCE_NS - Announce name service availability via RIP
 *
 * This function performs two operations:
 * 1. Registers the routing port with the remote name service
 * 2. Broadcasts a name service announcement packet
 *
 * The announcement is sent via PKT_$SEND_INTERNET to all nodes on
 * the network (dest_node = 0xFFFF), socket 8 (RIP socket).
 *
 * Called from:
 * - Function pointer table at 0xE7B32A (network initialization?)
 *
 * Original address: 0x00E6914E
 *
 * Assembly breakdown:
 *   00e6914e: link.w A6,-0xc               ; Local frame: 12 bytes
 *   00e69152: move.l #0xe245a4,-(SP)       ; argument 2: &NODE_$ME
 *   00e69158: move.l #0xe2e0a0,-(SP)       ; argument 1: &ROUTE_$PORT
 *   00e6915e: jsr REM_NAME_$REGISTER_SERVER
 *   00e69164: addq.w #8,SP                 ; two longword arguments popped
 *   00e69166: jsr PKT_$NEXT_ID             ; Get packet sequence ID
 *   00e6916c: move.w D0w,(-0xa,A6)         ; Store packet ID in local
 *   00e69170-00e691b8: Build and send internet packet
 */
void RIP_$ANNOUNCE_NS(void)
{
    uint16_t packet_id;
    uint16_t retry_hint;        /* A6-0x8 */
    uint16_t timeout_out;       /* A6-0x6 */
    status_$t status;           /* A6-0x4 */

    /*
     * Step 1: Register the routing port with the name service
     *
     * Two longword arguments are pushed and popped again by the "addq.w #0x8"
     * at 0x00E69164: &NODE_$ME (0xE245A4) at 0x00E69152 and &ROUTE_$PORT
     * (0xE2E0A0) at 0x00E69158.  The last push is argument 1, so the call is
     * (&ROUTE_$PORT, &NODE_$ME) - the (network, node) pair naming the server
     * we are registering.  REM_NAME_$REGISTER_SERVER (0x00E4A4AE) reads
     * neither, but the call site passes them, so the C call does too.
     *
     * 0xE2E0A0 is ROUTE_$PORT in the SAU2 map (aliased NETWORK_$ME there);
     * ROUTE_$PORT_ARRAY starts at the same address.
     */
    REM_NAME_$REGISTER_SERVER(&ROUTE_$PORT, &NODE_$ME);

    /*
     * Step 2: Get a unique packet ID for the announcement
     */
    packet_id = PKT_$NEXT_ID();

    /*
     * Step 3: Send the name service announcement (0x00E69170-0x00E691B2).
     * Argument order follows the pushes; both retry_hint and timeout_out are
     * word locals that PKT_$BLD_INTERNET_HDR writes through unconditionally
     * (0x00E1230E, 0x00E12316).
     */
    PKT_$SEND_INTERNET(
        0,                      /* 1  routing_key                           */
        0xFFFFF,                /* 2  dest_node: the broadcast node id      */
        RIP_SOCKET,             /* 3  dest_sock: 8                          */
        (int32_t)ROUTE_$PORT,   /* 4  src_node_or: 0xE2E0A0                 */
        NODE_$ME,               /* 5  src_node                              */
        0xFFFF,                 /* 6  src_sock                              */
        RIP_$BCAST_CONTROL,     /* 7  pkt_info: 0xE26EC0                    */
        packet_id,              /* 8  request_id                            */
        RIP_$NS_ANNOUNCEMENT,   /* 9  template: 0xE26EBE, 2 bytes           */
        2,                      /* 10 template_len                          */
        RIP_$ANNOUNCE_EXTRA,    /* 11 data: pea (-0x35a,PC) -> 0xE68E28     */
        0,                      /* 12 data_len                              */
        &retry_hint,            /* 13 retry_hint   A6-0x8                   */
        &timeout_out,           /* 14 timeout_out  A6-0x6                   */
        &status                 /* 15 status_ret   A6-0x4                   */
    );

    /*
     * The original never pops the argument block and never reads the status -
     * "unlk A6" at 0x00E691B8 discards it.
     */
    (void)status;
    (void)retry_hint;
    (void)timeout_out;
}

/*
 * RIP_$HALT_ROUTER - Gracefully stop the router
 *
 * This function is called when the number of routing ports drops to 1,
 * indicating that the router should stop advertising routes. It sends
 * a "poison" RIP response to all neighbors indicating that all routes
 * through this router are now unreachable (metric = 16).
 *
 * The halt packet structure at 0xE87D68:
 *   +0x00: 16 bytes header (zeros - filled by RIP_$SEND)
 *   +0x10: RIP data (8 bytes):
 *          - 00 02: Command 2 (Response)
 *          - FF FF FF FF: Network 0xFFFFFFFF (all networks)
 *          - 00 10: Metric 16 (unreachable)
 *
 * Parameters:
 *   @param flags  Route type to halt:
 *                   If < 0: Halt non-standard routes, send via IDP (-1 = 0xFF flags)
 *                   If >= 0: Halt standard routes, send via wired (flags = 0)
 *
 * Called from:
 * - ROUTE_$DECREMENT_PORT (0x00E69E40, port close handler) when port count drops to 1
 *
 * Original address: 0x00E87396
 *
 * Assembly breakdown:
 *   00e87396: link.w A6,-0x4
 *   00e8739a: pea (A5)                     ; Save A5
 *   00e8739c: lea (0xe87d68).l,A5          ; A5 = halt packet buffer
 *   00e873a2: move.b (0x8,A6),D0b          ; D0 = flags parameter
 *   00e873a6: bpl.b 0x00e873c6             ; If flags >= 0, branch to std
 *
 * Non-standard branch (flags < 0):
 *   00e873a8-00e873be: Call RIP_$SEND with flags=0xFF (negative, IDP send)
 *   00e873be: clr.b RIP_$STD_RECENT_CHANGES
 *
 * Standard branch (flags >= 0):
 *   00e873c6-00e873dc: Call RIP_$SEND with route_len=8, flags=0 (wired send)
 *   00e873dc: clr.b RIP_$RECENT_CHANGES
 */
void RIP_$HALT_ROUTER(boolean flags)
{
    /*
     * The halt packet at RIP_$HALT_PACKET (0xE87D68) contains:
     * - Header area (16 bytes, zeros)
     * - RIP response data at +0x10 (RIP_$HALT_PACKET_DATA):
     *   - Command: 2 (Response)
     *   - Network: 0xFFFFFFFF (all networks)
     *   - Metric: 16 (unreachable/infinity)
     *
     * This "poison reverse" packet tells all neighbors that routes
     * through this router are no longer valid.
     */
    if (flags < 0) {
        /*
         * Halt non-standard routing:
         * - Send via IDP method (flags = 0xFF, which is negative as signed byte)
         * - Clear the non-standard recent changes flag
         *
         * Note: The assembly uses 'st -(SP)' which sets a byte to -1 (0xFF),
         * then RIP_$SEND interprets flags < 0 as "use IDP send method"
         */
        RIP_$SEND(RIP_$HALT_PACKET, -1, RIP_$HALT_PACKET_DATA, 8, (int8_t)0xFF);
        RIP_$STD_RECENT_CHANGES = 0;
    } else {
        /*
         * Halt standard routing:
         * - Send via wired method (flags >= 0)
         * - Clear the standard recent changes flag
         *
         * The original pushes a single longword 0x00080000 for the last two
         * parameters (move.l #0x80000,-(SP)): its high word (8) is the
         * route_len parameter at (0x12,A6) and the high byte of its low word
         * (0x00) is the flags byte read at (0x14,A6).
         */
        RIP_$SEND(RIP_$HALT_PACKET, -1, RIP_$HALT_PACKET_DATA, 8, 0);
        RIP_$RECENT_CHANGES = 0;
    }
}
