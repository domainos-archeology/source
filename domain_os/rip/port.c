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

/*
 * Forward declaration for XNS_IDP_$OS_OPEN
 *
 * Note: This should eventually be in an xns_idp.h header.
 */
extern void XNS_IDP_$OS_OPEN(void *open_params, status_$t *status_ret);

/*
 * Structure for XNS_IDP_$OS_OPEN parameters (0x10 bytes)
 *
 * This structure is used to open an XNS/IDP channel. The channel allows
 * receiving packets destined for a specific port.
 *
 * Note: Partially understood structure; more fields may exist.
 */
typedef struct xns_idp_$open_params_t {
    uint16_t    socket;         /* 0x00: Socket number (high word of first long) */
    uint16_t    flags;          /* 0x02: Flags (low word: 0x0002 = ?) */
    uint32_t    port;           /* 0x04: Port identifier */
    void        (*demux)(void); /* 0x08: Demultiplexer callback function */
} xns_idp_$open_params_t;

/*
 * Structure for IDP packet header (used by RIP_$STD_DEMUX)
 *
 * This represents the layout of an incoming IDP packet as seen by
 * the demultiplexer. Offsets are relative to the packet structure base.
 */
typedef struct idp_$packet_t {
    uint8_t     _reserved0[0x1A];   /* 0x00: Unknown header fields */
    uint16_t    checksum;           /* 0x1A: Packet checksum or length field */
    uint32_t    src_network;        /* 0x1C: Source network address */
    uint8_t     _reserved1[0x06];   /* 0x20: Unknown fields */
    uint32_t    dest_network;       /* 0x26: Destination network address */
    uint16_t    dest_socket;        /* 0x2A: Destination socket */
    uint16_t    pkt_length;         /* 0x2C: Packet data length */
    uint8_t     _reserved2[0x08];   /* 0x2E: Unknown fields */
    uint16_t    rip_length;         /* 0x36: RIP data length */
    uint8_t     rip_data[16];       /* 0x38: RIP packet data (variable) */
} idp_$packet_t;

/*
 * Structure for SOCK_$PUT data (assembled from IDP packet)
 *
 * This is the data structure passed to SOCK_$PUT when queuing
 * RIP packets for processing by the RIP server.
 */
typedef struct rip_$demux_data_t {
    uint32_t    src_network;        /* 0x00: Source network address */
    uint32_t    dest_network;       /* 0x04: Destination network address */
    uint16_t    dest_socket;        /* 0x08: Destination socket */
    uint32_t    pkt_length;         /* 0x0A: Packet data length (extended to 32-bit) */
    uint16_t    _reserved;          /* 0x0E: Reserved/padding */
    uint16_t    checksum;           /* 0x10: Checksum from IDP header */
    uint16_t    rip_length;         /* 0x12: RIP data length */
    uint8_t     rip_data[16];       /* 0x14: RIP packet data */
} rip_$demux_data_t;

/* Status code returned on successful packet queue */
#define status_$sock_packet_queued      0x3B0016

/*
 * RIP_$STD_OPEN - Open standard RIP IDP channel
 *
 * Opens an XNS/IDP channel for receiving RIP packets on the standard
 * routing port. The channel uses RIP_$STD_DEMUX as its packet demultiplexer.
 *
 * On success, the channel number is stored in RIP_$STD_IDP_CHANNEL.
 * On failure, the channel number is not modified.
 *
 * Assembly analysis (0x00E15AAE):
 *   - Builds open_params on stack at -0x28(A6)
 *   - Sets flags = 0x10002 (socket 1, flags 2?)
 *   - Sets port = ROUTE_$PORT (from 0xE2E0A0)
 *   - Sets demux = RIP_$STD_DEMUX (0xE15A2C)
 *   - Calls XNS_IDP_$OS_OPEN
 *   - If status == 0, saves channel from params+2 to RIP_$STD_IDP_CHANNEL
 *
 * Original address: 0x00E15AAE
 */
void RIP_$STD_OPEN(void)
{
    status_$t status;

    /*
     * Build open parameters structure
     *
     * The first longword is 0x10002:
     *   - High word (0x0001) = socket number
     *   - Low word (0x0002) = flags
     *
     * This is stored differently in the decompilation - the socket/flags
     * are packed into the first 4 bytes.
     */
    struct {
        uint16_t    socket;         /* Socket 1 */
        uint16_t    flags;          /* Flags 0x0002 */
        uint32_t    port;           /* Port identifier */
        void        (*demux)(void); /* Demultiplexer callback */
    } open_params;

    open_params.socket = 0x0001;
    open_params.flags = 0x0002;
    open_params.port = ROUTE_$PORT;
    open_params.demux = (void (*)(void))RIP_$STD_DEMUX;

    XNS_IDP_$OS_OPEN(&open_params, &status);

    if (status == status_$ok) {
        /* Store the channel number (at offset 2 in params = flags field) */
        RIP_$STD_IDP_CHANNEL = open_params.flags;
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
 * 4. Returns status_$sock_packet_queued (0x3B0016) on success
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
    rip_$demux_data_t local_data;

    /* Extract destination info */
    local_data.dest_network = pkt->dest_network;
    local_data.dest_socket = pkt->dest_socket;
    local_data.pkt_length = (uint32_t)pkt->pkt_length;

    /* Extract source info */
    local_data.src_network = pkt->src_network;
    local_data.checksum = pkt->checksum;
    local_data.rip_length = pkt->rip_length;

    /* Reserved/padding set to zero */
    local_data._reserved = 0;

    /* Copy RIP data (16 bytes from offset 0x38) */
    local_data.rip_data[0] = pkt->rip_data[0];
    local_data.rip_data[1] = pkt->rip_data[1];
    local_data.rip_data[2] = pkt->rip_data[2];
    local_data.rip_data[3] = pkt->rip_data[3];
    local_data.rip_data[4] = pkt->rip_data[4];
    local_data.rip_data[5] = pkt->rip_data[5];
    local_data.rip_data[6] = pkt->rip_data[6];
    local_data.rip_data[7] = pkt->rip_data[7];
    local_data.rip_data[8] = pkt->rip_data[8];
    local_data.rip_data[9] = pkt->rip_data[9];
    local_data.rip_data[10] = pkt->rip_data[10];
    local_data.rip_data[11] = pkt->rip_data[11];
    local_data.rip_data[12] = pkt->rip_data[12];
    local_data.rip_data[13] = pkt->rip_data[13];
    local_data.rip_data[14] = pkt->rip_data[14];
    local_data.rip_data[15] = pkt->rip_data[15];

    /* Queue packet to RIP socket (socket 8) */
    result = SOCK_$PUT(RIP_SOCKET, (void **)&local_data, 0, *param_2, *param_3);

    /* If successful (result >= 0), set status indicating packet queued */
    if (result >= 0) {
        *status_ret = status_$sock_packet_queued;
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
void RIP_$PORT_CLOSE(uint16_t port_index, int8_t flags, int8_t force)
{
    int i;
    rip_$route_t *route;
    rip_$entry_t *entry;
    uint8_t state;

    RIP_$LOCK();

    /* Iterate through all routing table entries */
    for (i = 0; i < RIP_TABLE_SIZE; i++) {
        entry = &RIP_$DATA.entries[i];

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
            RIP_$STD_RECENT_CHANGES = (int8_t)0xFF;
        } else {
            RIP_$RECENT_CHANGES = (int8_t)0xFF;
        }
    }

    RIP_$UNLOCK();
}
