/*
 * RIP Port Management Functions
 *
 * This file contains functions for managing RIP ports including opening
 * IDP channels, demultiplexing incoming packets, and closing ports.
 *
 * Functions:
 * - RIP_$STD_OPEN      Opens standard RIP IDP channel
 * - RIP_$STD_DEMUX     Demultiplexes incoming RIP packets
 * - RIP_$PORT_CLOSE    Invalidates routes through a port
 *
 * Original addresses:
 * - RIP_$STD_OPEN:     0x00E15AAE
 * - RIP_$STD_DEMUX:    0x00E15A2C
 * - RIP_$PORT_CLOSE:   0x00E15798
 */

#include "rip/rip_internal.h"
#include "sock/sock.h"
#include "xns_idp/xns_idp.h"

/*
 * The record RIP_$STD_DEMUX hands SOCK_$PUT is a sock_$pkt_info_t, not a
 * private layout (bead source-dxxz).  The frame local starts at A6-0x40 and
 * every store lands on one of that record's fields:
 *
 *   00e15a52  move.l (0x1c,A0),(-0x40,A6)   +0x00  hdr
 *   00e15a3c  move.l (0x26,A0),(-0x3c,A6)   +0x04  src_addr
 *   00e15a42  move.w (0x2a,A0),(-0x38,A6)   +0x08  src_port
 *   00e15a4a  move.w (0x2c,A0),D0w / move.l D0,(-0x34,A6)
 *                                            +0x0C  dst_addr, zero-extended
 *   00e15a36  move.w #0x2,(-0x30,A6)        +0x10  flags = SOCK_PKT_FLAG_XNS
 *   00e15a74  clr.w (-0x2e,A6)              +0x12  n_hops = 0
 *   00e15a5e  move.w (0x36,A0),(-0x16,A6)   +0x2A  data_len
 *   00e15a58  move.w (0x1a,A0),(-0x14,A6)   +0x2C  hdr_len
 *   00e15a6c  four "move.l (A1)+,(A2)+"     +0x30  data_pages[4] from pkt+0x38
 *
 * The hops array at +0x14..+0x29 is left as the frame found it, which is why
 * the function reserves 0x40 bytes and writes only these fields.
 */

/* Status code returned on successful packet queue */

/*
 * RIP_$STD_OPEN - Open standard RIP IDP channel
 *
 * Opens an XNS/IDP channel for receiving RIP packets on the standard
 * routing port. The channel uses RIP_$STD_DEMUX as its packet demultiplexer.
 *
 * On success, the channel number is stored in RIP_$STD_IDP_CHANNEL.
 * On failure, the channel number is not modified.
 *
 * Assembly (0x00E15AAE), record base A6-0x28:
 *
 *   00e15aae  link.w  A6,-0x2c
 *   00e15aba  move.l  #0x10002,(-0x28,A6)          ; +0x00 socket 1, flags 2
 *   00e15ac2  move.l  (0x00e2e0a0).l,(-0x20,A6)    ; +0x08 network = ROUTE_$PORT
 *   00e15aca  move.l  #0xe15a2c,(-0x24,A6)         ; +0x04 demux = RIP_$STD_DEMUX
 *   00e15ad2  pea     (-0x2c,A6)                   ; status
 *   00e15ad6  pea     (-0x28,A6)                   ; the option record
 *   00e15ada  jsr     0x00e17f02.l                 ; XNS_IDP_$OS_OPEN
 *   00e15ae2  tst.l   (-0x2c,A6)
 *   00e15ae8  move.w  (-0x26,A6),(0xc64,A5)        ; +0x02, the channel index
 *
 * The record is xns_$os_open_opt_t (xns/xns.h), so the DEMUX vector is the
 * longword at +0x04 and the network at +0x08 - the other way round from the
 * anonymous struct this file used to declare, which also made the call
 * type-incompatible with the prototype.
 *
 * Only +0x00..+0x0B are written.  XNS_IDP_$OS_OPEN goes on to read the
 * source address at +0x0C (0x00E18008) and the destination at +0x18
 * (0x00E18052), so it sees whatever this frame happened to contain; that is
 * reproduced here by leaving those members uninitialised.
 *
 * Original address: 0x00E15AAE
 */
void RIP_$STD_OPEN(void)
{
    status_$t status;
    xns_$os_open_opt_t open_params;

    /* 0x00E15ABA: one longword, 0x00010002. */
    open_params.socket        = 0x0001;
    open_params.flags_channel = 0x0002;

    /* 0x00E15ACA: the demux vector, a code address moved as one longword. */
    open_params.demux = ARCH_PTR_TO_VA((void *)RIP_$STD_DEMUX);

    /* 0x00E15AC2 */
    open_params.network = ROUTE_$PORT;

    XNS_IDP_$OS_OPEN(&open_params, &status);

    if (status == status_$ok) {
        /* 0x00E15AE8: the OUT channel index is the word at +0x02. */
        RIP_$WIRED_DATA.std_idp_channel = (int16_t)open_params.flags_channel;
    }
}

/*
 * RIP_$STD_DEMUX - Demultiplex incoming RIP packets
 *
 * This is the demultiplexer callback invoked by XNS_IDP when a packet
 * arrives on the RIP channel. It extracts relevant information from
 * the IDP packet and queues it for the RIP server via SOCK_$PUT.
 *
 * Parameters (per Ghidra analysis):
 * @param pkt           Pointer to IDP packet structure
 * @param param_2       Pointer to 2-byte value (event count param 1)
 * @param param_3       Pointer to 2-byte value (event count param 2)
 * @param param_4       Unused
 * @param status_ret    Output: status code
 *
 * The function:
 * 1. Extracts network addresses, socket, and length from IDP header
 * 2. Copies RIP-specific data from packet
 * 3. Queues to socket 8 (RIP_SOCKET) via SOCK_$PUT
 * 4. Returns status_$xns_could_not_put_packet_into_socket (0x3B0016) on success
 *
 * Assembly analysis (0x00E15A2C):
 *   - Builds local buffer from IDP packet fields
 *   - param_1+0x26 = dest_network, +0x2a = dest_socket, +0x2c = pkt_length
 *   - param_1+0x1c = src_network, +0x1a = checksum
 *   - param_1+0x36..0x44 = RIP data (16 bytes)
 *   - Calls SOCK_$PUT(8, &buffer, 0, *param_2, *param_3)
 *   - If result >= 0, sets *status_ret = 0x3B0016
 *
 * Original address: 0x00E15A2C
 */
void RIP_$STD_DEMUX(idp_$packet_t *pkt, uint16_t *param_2, uint16_t *param_3,
                    void *param_4, status_$t *status_ret)
{
    int8_t result;
    sock_$pkt_info_t rec;           /* A6-0x40 */
    int i;

    (void)param_4;                  /* the 0x14 argument is never read */

    /* 0xE15A36: the frame arrived over XNS ("standard") routing */
    rec.flags = SOCK_PKT_FLAG_XNS;

    /* 0xE15A3C / 0xE15A42 */
    rec.src_addr = pkt->dest_network;
    rec.src_port = pkt->dest_socket;

    /* 0xE15A48: "clr.l D0 / move.w (0x2c,A0),D0w" - a zero-extended word */
    rec.dst_addr = (uint32_t)pkt->pkt_length;

    /* 0xE15A52 / 0xE15A58 / 0xE15A5E */
    rec.hdr = pkt->src_network;
    rec.hdr_len = pkt->checksum;
    rec.data_len = pkt->rip_length;

    /*
     * 0xE15A64-0xE15A72: four "move.l (A1)+,(A2)+" from pkt+0x38.  The
     * source is a byte array in the header record, so each longword is
     * rebuilt big-endian here rather than memcpy'd: that keeps the VALUE the
     * m68k would have loaded on a little-endian host too.
     */
    for (i = 0; i < 4; i++) {
        rec.data_pages[i] =
            ((uint32_t)pkt->rip_data[i * 4 + 0] << 24) |
            ((uint32_t)pkt->rip_data[i * 4 + 1] << 16) |
            ((uint32_t)pkt->rip_data[i * 4 + 2] << 8) |
            ((uint32_t)pkt->rip_data[i * 4 + 3]);
    }

    /* 0xE15A74 */
    rec.n_hops = 0;

    /* 0xE15A8E: SOCK_$PUT(8, &rec, 0, *param_2, *param_3) */
    result = SOCK_$PUT(RIP_SOCKET, &rec, 0, *param_2, *param_3);

    /* 0xE15A98 "tst.b D0b / bmi" - a negative result skips the store */
    if (result >= 0) {
        *status_ret = status_$xns_could_not_put_packet_into_socket;
    }
}

/*
 * RIP_$PORT_CLOSE - Invalidate routes through a closing port
 *
 * When a port is being closed, this function marks all routes using
 * that port as expired. This ensures the routing table is updated
 * to remove routes that are no longer reachable.
 *
 * The function iterates through all routing table entries and for
 * each route using the specified port:
 * 1. Sets metric to infinity (0x11)
 * 2. Sets state to EXPIRED (flags |= 0xC0)
 * 3. Sets expiration time to allow graceful aging
 * 4. Sets the appropriate recent_changes flag
 *
 * Parameters:
 * @param port_index    Port index (0-7) being closed
 * @param flags         If < 0, process non-standard routes; else standard
 * @param force         If < 0, invalidate all routes on port;
 *                      If >= 0, only invalidate routes with non-zero metric
 *
 * Assembly analysis (0x00E15798):
 *   - A5 = RIP_$DATA base (0xE26258)
 *   - D2 = port_index, D3 = flags, D4 = force
 *   - Calls RIP_$LOCK
 *   - Loops 64 times (0x3F + 1):
 *     - If flags < 0: A3 = entry + 0x17C (non-std route)
 *       else: A3 = entry + 0x168 (std route)
 *     - Check: (route->flags >> 6) != 0 (route is in use)
 *     - Check: route->port == port_index
 *     - Check: force < 0 OR route->metric != 0
 *     - If all pass:
 *       - route->metric = 0x11 (infinity)
 *       - route->flags |= 0xC0 (set state to EXPIRED)
 *       - route->expiration = TIME_$CLOCKH + 0x168
 *       - Set recent_changes flag (0xC86 or 0xC88)
 *   - Calls RIP_$UNLOCK
 *
 * Note on offsets:
 *   - Entry base at RIP_$DATA + 0x164 (not 0x168 as in asm - asm uses A2 offset)
 *   - Standard route at entry + 0x04
 *   - Non-standard route at entry + 0x18
 *   - The assembly uses offsets from RIP_$DATA directly:
 *     0x168 = 0x164 + 0x04 (std route in first entry)
 *     0x17C = 0x164 + 0x18 (non-std route in first entry)
 *
 * Original address: 0x00E15798
 */
void RIP_$PORT_CLOSE(uint16_t port_index, boolean flags, boolean force)
{
    int i;
    rip_$route_t *route;
    rip_$entry_t *entry;
    uint8_t state;

    RIP_$LOCK();

    /* Iterate through all routing table entries */
    for (i = 0; i < RIP_TABLE_SIZE; i++) {
        entry = &RIP_$WIRED_DATA.info[i];

        /* Select standard or non-standard route based on flags */
        if (flags < 0) {
            /* Non-standard route (index 1) */
            route = &entry->routes[1];
        } else {
            /* Standard route (index 0) */
            route = &entry->routes[0];
        }

        /* Extract route state from flags (top 2 bits) */
        state = (route->flags >> RIP_STATE_SHIFT) & 0x03;

        /* Skip if route is unused (state == 0) */
        if (state == RIP_STATE_UNUSED) {
            continue;
        }

        /* Check if route uses this port */
        if (route->port != port_index) {
            continue;
        }

        /*
         * If force >= 0, only invalidate routes with non-zero metric.
         * If force < 0, invalidate all routes on this port.
         */
        if (force >= 0 && route->metric == 0) {
            continue;
        }

        /* Mark route as expired */
        route->metric = RIP_INFINITY;
        route->flags |= RIP_STATE_MASK;  /* Set both state bits = EXPIRED */

        /* Set expiration time (allow some time for route update propagation) */
        route->expiration = TIME_$CLOCKH + RIP_ROUTE_TIMEOUT;

        /* Signal that routes have changed */
        if (flags < 0) {
            RIP_$WIRED_DATA.std_recent_changes = (int8_t)0xFF;
        } else {
            RIP_$WIRED_DATA.recent_changes = (int8_t)0xFF;
        }
    }

    RIP_$UNLOCK();
}
