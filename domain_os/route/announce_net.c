/*
 * ROUTE_$ANNOUNCE_NET - Announce network to mother node
 *
 * When running diskless, this function sends a broadcast control packet
 * to the mother node to announce this node's network address. This is
 * part of the diskless boot protocol where the node needs to register
 * its network identity with the mother (boot server).
 *
 * The function:
 *   1. Checks if running in diskless mode (NETWORK_$DISKLESS < 0)
 *   2. Copies the RIP broadcast control template
 *   3. Clears the high bit of byte 1 (removes a flag)
 *   4. Sends the packet via PKT_$SEND_INTERNET to the mother node
 *
 * Original address: 0x00E69FB2
 *
 * Called from:
 *   - ROUTE_$SERVICE at 0x00E6A308 (when network address changes on port 0)
 */

#include "route/route_internal.h"
#include "network/network.h"
#include "pkt/pkt.h"

/*
 * External data:
 *   NETWORK_$DISKLESS (0xE24C4C), NETWORK_$MOTHER_NODE (0xE24C0C) and
 *   NODE_$ME (0xE245A4) come from network/network.h;
 *   RIP_$BCAST_CONTROL (30 byte template at 0xE26EC0) from rip/rip.h.
 */

/*
 * Size of broadcast control packet
 */
#define BCAST_CONTROL_SIZE      0x1E

/*
 * =============================================================================
 * Implementation
 * =============================================================================
 */

/*
 * ROUTE_$ANNOUNCE_NET - Announce network to mother node
 *
 * @param network   Network address to announce
 */
void ROUTE_$ANNOUNCE_NET(uint32_t network)
{
    uint8_t control_packet[BCAST_CONTROL_SIZE];
    status_$t status;
    uint8_t dummy1[2], dummy2[2];
    uint16_t packet_id;

    /*
     * Only send announcement if running diskless
     * The original code checks if NETWORK_$DISKLESS < 0
     */
    if (NETWORK_$DISKLESS >= 0) {
        return;
    }

    /*
     * Copy the broadcast control template
     * Original uses a word-copy loop for efficiency
     */
    for (int i = 0; i < BCAST_CONTROL_SIZE; i++) {
        control_packet[i] = RIP_$BCAST_CONTROL[i];
    }

    /*
     * Clear bit 7 of byte 1
     * This removes a flag from the control packet
     * Original: bclr.b #0x7,(-0x27,A6)
     */
    control_packet[1] &= 0x7F;

    /*
     * Get next packet ID
     */
    packet_id = PKT_$NEXT_ID();

    /*
     * Send the announcement packet to the mother node
     *
     * Parameters:
     *   - dest_net: The network being announced
     *   - dest_node: NETWORK_$MOTHER_NODE (boot server)
     *   - dest_socket: 8 (routing protocol socket)
     *   - src_net: ROUTE_$PORT (this node's port network)
     *   - src_node: NODE_$ME (this node's address)
     *   - src_socket: 8 (routing protocol socket)
     *   - data: The control packet
     *   - packet_id: From PKT_$NEXT_ID()
     *   - Remaining parameters are protocol extras
     */
    PKT_$SEND_INTERNET(
        network,                /* Destination network */
        NETWORK_$MOTHER_NODE,   /* Destination node */
        8,                      /* Destination socket (routing) */
        ROUTE_$PORT,            /* Source network */
        NODE_$ME,               /* Source node */
        8,                      /* Source socket (routing) */
        control_packet,         /* Packet data */
        packet_id,              /* Packet ID */
        NULL,                   /* Extra parameter 1 */
        2,                      /* Extra parameter 2 */
        NULL,                   /* Extra parameter 3 (at 0xe6a02c = 0) */
        0,                      /* Extra parameter 4 */
        dummy1,                 /* Extra parameter 5 */
        dummy2,                 /* Extra parameter 6 */
        &status                 /* Output status */
    );
}
