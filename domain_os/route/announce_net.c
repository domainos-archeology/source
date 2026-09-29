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
 * Size of the broadcast control record copied out of RIP_$BCAST_CONTROL
 * ("moveq #0x6,D0" + dbf = 7 longwords, then one more word: 0x00E69FC8 -
 * 0x00E69FD2).
 */
#define BCAST_CONTROL_SIZE      0x1E

/*
 * PC-relative constant cell of the original: 0x00E6A02C is a longword zero
 * passed as PKT_$SEND_INTERNET's "data" ("pea (0x40,PC)" at 0x00E69FEA,
 * whose target is 0x00E69FEC + 0x40).  data_len is zero, so PKT_$SEND_INTERNET
 * never dereferences it (0x00E12686 "tst.w D6w / ble").
 *
 * ROUTE_$SERVICE takes the address of the SAME cell with "pea (-0x40c,PC)"
 * at 0x00E6A436, so this file shares route_$null_service_rec rather than
 * defining a private static of its own.  (source-l8qy)
 */

/*
 * =============================================================================
 * Implementation
 * =============================================================================
 */

/*
 * ROUTE_$ANNOUNCE_NET - Announce network to mother node
 *
 * @param network   Network address to announce
 *
 * Original address: 0x00E69FB2
 */
void ROUTE_$ANNOUNCE_NET(uint32_t network)
{
    uint8_t     control_packet[BCAST_CONTROL_SIZE];  /* A6-0x28 */
    status_$t   status;             /* A6-0x2C */
    uint16_t    timeout_out;        /* A6-0x2E */
    uint16_t    retry_hint;         /* A6-0x30 */
    uint16_t    packet_id;
    int         i;

    /* 0x00E69FB6: only announce while diskless */
    if (NETWORK_$DISKLESS >= 0) {
        return;
    }

    /* 0x00E69FBE-0x00E69FD2: copy the 30-byte control template */
    for (i = 0; i < BCAST_CONTROL_SIZE; i++) {
        control_packet[i] = RIP_$BCAST_CONTROL[i];
    }

    /*
     * 0x00E69FD4 "bclr.b #0x7,(-0x27,A6)": clear bit 7 of the record's second
     * byte, i.e. bit 15 of its first word.
     */
    control_packet[1] &= (uint8_t)0x7F;

    /* 0x00E69FF6 */
    packet_id = PKT_$NEXT_ID();

    /*
     * 0x00E69FDA-0x00E6A020.  Argument order follows the pushes; both
     * retry_hint and timeout_out are word locals that
     * PKT_$BLD_INTERNET_HDR writes through unconditionally.
     */
    PKT_$SEND_INTERNET(
        network,                        /* 1  routing_key   (0x8,A6)         */
        NETWORK_$MOTHER_NODE,           /* 2  dest_node     0xE24C0C         */
        8,                              /* 3  dest_sock                      */
        (int32_t)ROUTE_$PORT,           /* 4  src_node_or   0xE2E0A0         */
        NODE_$ME,                       /* 5  src_node      0xE245A4         */
        8,                              /* 6  src_sock                       */
        control_packet,                 /* 7  pkt_info      A6-0x28          */
        packet_id,                      /* 8  request_id                     */
        (void *)&ROUTE_$UNWIRED_DATA.announce_template, /* 9  template   pea (0x4,A5)      */
        2,                              /* 10 template_len                   */
        (void *)route_$null_service_rec, /* 11 data  pea (0x40,PC) = 0xE6A02C */
        0,                              /* 12 data_len                       */
        &retry_hint,                    /* 13 retry_hint    A6-0x30          */
        &timeout_out,                   /* 14 timeout_out   A6-0x2E          */
        &status                         /* 15 status_ret    A6-0x2C          */
    );

    /*
     * The original never pops the arguments and never reads status - "unlk
     * A6" at 0x00E6A026 discards the whole block.
     */
    (void)status;
    (void)retry_hint;
    (void)timeout_out;
}
