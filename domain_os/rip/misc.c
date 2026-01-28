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

/*
 * External function prototypes
 *
 * TODO: REM_NAME_$REGISTER_SERVER has signature conflict between this usage
 * (with parameters) and name/name.h declaration (no parameters). The decompiled
 * code clearly passes parameters, so the header may be incorrect.
 */
extern void REM_NAME_$REGISTER_SERVER(void *port_array, void *node_id);

/*
 * External data references
 */

#if defined(ARCH_M68K)
    /* RIP_$BCAST_CONTROL - Broadcast control parameters (30 bytes) */
    #define RIP_$BCAST_CONTROL      ((void *)0xE26EC0)

    /* RIP_$NS_ANNOUNCEMENT - Name service announcement data (2+ bytes) */
    #define RIP_$NS_ANNOUNCEMENT    ((void *)0xE26EBE)

    /* ROUTE_$PORT_ARRAY - Array of port information (first entry = ROUTE_$PORT) */
    #define ROUTE_$PORT_ARRAY       ((void *)0xE2E0A0)

    /* NODE_$ME_PTR - Pointer to this node's ID */
    #define NODE_$ME_PTR            ((void *)0xE245A4)

    /* Extra data reference for PKT_$SEND_INTERNET (likely empty/zero) */
    #define RIP_$ANNOUNCE_EXTRA     ((void *)0xE68E28)

    /* RIP halt packet data buffer (16 bytes header + 8 bytes RIP data) */
    #define RIP_$HALT_PACKET        ((void *)0xE87D68)
    #define RIP_$HALT_PACKET_DATA   ((void *)0xE87D78)
#else
    extern uint8_t RIP_$BCAST_CONTROL[30];
    extern uint8_t RIP_$NS_ANNOUNCEMENT[2];
    extern uint8_t ROUTE_$PORT_ARRAY[];
    extern uint32_t NODE_$ME_PTR;
    extern uint8_t RIP_$ANNOUNCE_EXTRA[4];
    extern uint8_t RIP_$HALT_PACKET[24];
    extern uint8_t RIP_$HALT_PACKET_DATA[8];
#endif

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
 *   00e69152: move.l #0xe245a4,-(SP)       ; Push NODE_$ME address
 *   00e69158: move.l #0xe2e0a0,-(SP)       ; Push ROUTE_$PORT_ARRAY address
 *   00e6915e: jsr REM_NAME_$REGISTER_SERVER
 *   00e69164: addq.w #8,SP
 *   00e69166: jsr PKT_$NEXT_ID             ; Get packet sequence ID
 *   00e6916c: move.w D0w,(-0xa,A6)         ; Store packet ID in local
 *   00e69170-00e691b8: Build and send internet packet
 */
void RIP_$ANNOUNCE_NS(void)
{
    uint16_t packet_id;
    uint8_t out1[2];
    uint8_t out2[2];
    status_$t status;

    /*
     * Step 1: Register the routing port with the name service
     *
     * REM_NAME_$REGISTER_SERVER takes:
     * - ROUTE_$PORT_ARRAY: Array of routing port information
     * - NODE_$ME_PTR: Pointer to this node's identifier
     */
    REM_NAME_$REGISTER_SERVER(ROUTE_$PORT_ARRAY, NODE_$ME_PTR);

    /*
     * Step 2: Get a unique packet ID for the announcement
     */
    packet_id = PKT_$NEXT_ID();

    /*
     * Step 3: Send the name service announcement
     *
     * The packet is sent with:
     * - dest_network = 0: Local network
     * - dest_net_ext = 0xFFFFF: Extended broadcast address
     * - socket = 8: RIP socket
     * - src_port = ROUTE_$PORT: Our routing port
     * - src_node = NODE_$ME: Our node ID
     * - dest_node = 0xFFFF: Broadcast to all nodes
     * - data = RIP_$NS_ANNOUNCEMENT: 2-byte announcement data
     */
    PKT_$SEND_INTERNET(
        0,                      /* dest_network: local */
        0xFFFFF,                /* dest_net_ext: broadcast */
        RIP_SOCKET,             /* socket: 8 */
        ROUTE_$PORT,            /* src_port */
        NODE_$ME,               /* src_node */
        0xFFFF,                 /* dest_node: broadcast */
        RIP_$BCAST_CONTROL,     /* broadcast control params */
        packet_id,              /* packet ID */
        RIP_$NS_ANNOUNCEMENT,   /* data: NS announcement */
        2,                      /* data_len: 2 bytes */
        RIP_$ANNOUNCE_EXTRA,    /* extra_data (likely empty) */
        0,                      /* extra_len */
        out1,                   /* output buffer 1 */
        out2,                   /* output buffer 2 */
        &status                 /* output status */
    );
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
 *                   If >= 0: Halt standard routes, send via wired (0x80000 flags)
 *
 * Called from:
 * - FUN_00E69E40 (port close handler) when port count drops to 1
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
 *   00e873c6-00e873dc: Call RIP_$SEND with flags=0x80000 (positive, wired send)
 *   00e873dc: clr.b RIP_$RECENT_CHANGES
 */
void RIP_$HALT_ROUTER(int16_t flags)
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
        RIP_$SEND(RIP_$HALT_PACKET, -1, RIP_$HALT_PACKET_DATA, RIP_SOCKET, 0xFF);
        RIP_$STD_RECENT_CHANGES = 0;
    } else {
        /*
         * Halt standard routing:
         * - Send via wired method (flags = 0x80000, positive)
         * - Clear the standard recent changes flag
         *
         * The 0x80000 value is chosen because it's positive (uses wired send)
         * but still has bit 19 set which may have other significance
         */
        RIP_$SEND(RIP_$HALT_PACKET, -1, RIP_$HALT_PACKET_DATA, RIP_SOCKET, 0x80000);
        RIP_$RECENT_CHANGES = 0;
    }
}
