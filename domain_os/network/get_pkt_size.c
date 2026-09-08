/*
 * NETWORK_$GET_PKT_SIZE - pick the packet size to use for a destination
 *
 * Asks RIP where the destination lives and answers with the caller's size when
 * the destination is this node, and with the 0x400 floor otherwise.  The
 * result is finally clamped to the caller's size and up to 0x400.
 *
 * Original address: 0x00E0FA00, size 230 bytes (0x00E0FA00-0x00E0FAE5).
 * A5 = 0x00E248FC (the NETWORK_ module data base).
 */

#include "network/network_internal.h"
#include "route/route.h"
#include "rip/rip.h"
#include "node/node.h"      /* NODE_$ME (0x00E245A4) */

/* Minimum/default packet size */
#define PKT_SIZE_MIN    0x400

/*
 * NETWORK_$GET_PKT_SIZE
 *
 * Parameters (0x08, 0x0C off A6):
 *   dest_addr - a two-longword destination {network, node}
 *   max_size  - the caller's requested maximum, by value as a word
 *
 * Returns the size to use, in D0.
 */
uint16_t NETWORK_$GET_PKT_SIZE(uint32_t *dest_addr, uint16_t max_size)
{
    uint16_t            result;         /* D0 */
    int16_t             rip_result;     /* D1 */
    int16_t             port_num;       /* A6-0x32 */
    status_$t           status;         /* A6-0x2c */
    rip_$nexthop_t      nexthop;        /* A6-0x20 */
    rip_$dest_addr_t    dest;           /* A6-0x10 */
    uint32_t            node;           /* A6-0x28 */
    uint32_t            network;
    route_$port_t      *port_ptr;

    result = max_size;

    /* 0x00E0FA16: a request already at the floor is answered immediately */
    if (max_size == PKT_SIZE_MIN) {
        goto clamp;
    }

    /*
     * 0x00E0FA1C-0x00E0FA2A: only this internet's own network number, or 0,
     * is worth a route lookup.
     */
    network = dest_addr[0];
    if (network != ROUTE_$PORT && network != 0) {
        result = PKT_SIZE_MIN;
        goto clamp;
    }

    /* 0x00E0FA2C-0x00E0FA40 */
    if (NETWORK_$LOOPBACK_FLAG < 0) {
        node = NODE_$ME;
    } else {
        node = dest_addr[1];
    }

    /*
     * 0x00E0FA42-0x00E0FA54: build the destination RIP is asked about.
     *   move.l D0,(-0x10,A6)            dest.network
     *   andi.l #-0x100000,(-0xa,A6)     dest.host_lo &= 0xFFF00000
     *   move.l (-0x28,A6),D1
     *   or.l   D1,(-0xa,A6)             dest.host_lo |= node
     * Only those two fields are written: dest.host_hi (A6-0x0c) and the top
     * twelve bits of dest.host_lo keep whatever the stack held, and
     * dest.socket is never touched.  That is the image's behaviour; the tree
     * used to leave the whole host address unwritten (bead source-zbqh).
     */
    dest.network = network;
    dest.host_lo = (dest.host_lo & 0xFFF00000u) | node;

    /*
     * 0x00E0FA56-0x00E0FA72: five arguments plus a word result slot.
     *   pea (-0x2c,A6)   status
     *   pea (-0x20,A6)   nexthop
     *   pea (-0x32,A6)   port number
     *   clr.w -(SP)      flags, false
     *   pea (-0x10,A6)   the destination
     */
    rip_result = RIP_$FIND_NEXTHOP(&dest, 0, &port_num, &nexthop, &status);

    /*
     * 0x00E0FA74-0x00E0FA82: D0 keeps the RIP result unless the lookup failed,
     * and D1 keeps the result unconditionally for the test below.
     */
    result = (uint16_t)rip_result;
    if (status != status_$ok) {
        result = PKT_SIZE_MIN;
    }

    /* 0x00E0FA84-0x00E0FA92: talking to ourselves - the caller's size stands */
    if (node == NODE_$ME) {
        result = max_size;
        goto clamp;
    }

    /* 0x00E0FA94: an indirect route always drops to the floor */
    if (rip_result != 0) {
        result = PKT_SIZE_MIN;
        goto clamp;
    }

    /*
     * 0x00E0FA98-0x00E0FAC6.  A direct route on a port whose "active" word is
     * 1 leaves D0 alone and writes status_$network_request_denied_by_local_node
     * into the LOCAL status cell - which nothing ever reads, since the routine
     * returns only D0.  Any other port drops to the floor; the
     * "movea.l (0x48,A0),A0" at 0x00E0FAB6 loads the driver record into a
     * register that is then never used.
     */
    port_ptr = ROUTE_$PORTP[port_num];
    if (port_ptr->active == 1) {
        status = status_$network_request_denied_by_local_node;
        goto clamp;
    }
    result = PKT_SIZE_MIN;

clamp:
    /* 0x00E0FACC-0x00E0FADA, both comparisons unsigned */
    if (result > max_size) {
        result = max_size;
    }
    if (result <= PKT_SIZE_MIN) {
        result = PKT_SIZE_MIN;
    }

    return result;
}
