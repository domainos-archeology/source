/*
 * RIP_$SEND - RIP Protocol Send Functions
 *
 * This file implements the RIP send and broadcast functions for transmitting
 * routing information packets across the network.
 *
 * Functions implemented:
 * - RIP_$SEND_TO_PORT: Send RIP packet to a specific port via XNS/IDP
 * - RTWIRED_PROC_START: Send RIP packet to a wired/local port
 * - RIP_$SEND: Main dispatcher - sends to one or all ports
 * - RIP_$BROADCAST: Build and broadcast full routing table
 *
 * Original addresses:
 * - RIP_$SEND_TO_PORT:    0x00E870DC
 * - RTWIRED_PROC_START:   0x00E87000
 * - RIP_$SEND:            0x00E871B6
 * - RIP_$BROADCAST:       0x00E87298
 */

#include "rip/rip_internal.h"
#include "route/route_internal.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "ec/ec.h"
#include "net_io/net_io.h"

/*
 * =============================================================================
 * External Function Prototypes
 * =============================================================================
 */

/* XNS/IDP functions from xns/xns.h (via xns_idp/xns_idp.h) */
#include "xns_idp/xns_idp.h"

/* Data copy function from os/os.h */
#include "os/os.h"

/*
 * =============================================================================
 * Global Data References
 * =============================================================================
 */

#if defined(ARCH_M68K)
    /* RIP IDP channel - 0xFFFF means no channel open */
    #define RIP_$STD_IDP_CHANNEL    (*(int16_t *)0xE26EBC)

    /* Broadcast control parameters (30 bytes) */
    #define RIP_$BCAST_CONTROL      ((void *)0xE26EC0)

    /* Port array base as uint8_t* for byte-level access to fields.
     * Note: ROUTE_$PORT_ARRAY is defined in route_internal.h as route_$port_t*,
     * so we use a different name here for byte-offset calculations. */
    #define ROUTE_$PORT_ARRAY_BYTES ((uint8_t *)0xE2E0A0)

    /* This node's ID */
    #define NODE_$ME                (*(uint32_t *)0xE245A4)

    /* RIP_$INFO - Base of routing table entries */
    #define RIP_$INFO               ((rip_$entry_t *)0xE263BC)

    /* Send callback address (static code, not a function pointer per se) */
    #define RIP_$SEND_CALLBACK      ((void *)0xE870D8)
#else
    extern int16_t RIP_$STD_IDP_CHANNEL;
    extern uint8_t RIP_$BCAST_CONTROL[30];
    extern uint8_t ROUTE_$PORT_ARRAY_BYTES[];
    extern uint32_t NODE_$ME;
    extern rip_$entry_t *RIP_$INFO;
    extern void *RIP_$SEND_CALLBACK;
#endif

/*
 * Port entry structure (partial, at ROUTE_$PORT_ARRAY)
 *
 * Each port entry is 0x5C (92) bytes. Key offsets:
 * +0x00: Network address (4 bytes)
 * +0x20: Alternate network address (4 bytes)
 * +0x24: Host address part 1
 * +0x28: Host address part 2
 * +0x2C: Port flags (2 bytes) - bit field controlling routing behavior
 * +0x2E: Port state (2 bytes) - 2 = active
 * +0x38: Event counter (ec_$eventcount_t)
 */
#define PORT_ENTRY_SIZE         0x5C
#define PORT_NETWORK_OFF        0x00
#define PORT_ALT_NETWORK_OFF    0x20
#define PORT_HOST_OFF           0x24
#define PORT_FLAGS_OFF          0x2C
#define PORT_STATE_OFF          0x2E
#define PORT_EVENTCOUNT_OFF     0x38

/* Port state values */
#define PORT_STATE_ACTIVE       2

/* Port flag bits for routing decisions */
#define PORT_FLAG_MASK_STD      0x28    /* Bits 3,5 for standard routes */
#define PORT_FLAG_MASK_NONSTD   0x30    /* Bits 4,5 for non-standard routes */

/*
 * XNS/IDP packet header structure for RIP_$SEND_TO_PORT
 *
 * This is the 30-byte (0x1E) IDP packet header built for RIP sends.
 */
typedef struct rip_$send_hdr_t {
    uint16_t    checksum;           /* 0x00: Checksum (0xFFFF = no checksum) */
    uint16_t    length;             /* 0x02: Packet length */
    uint8_t     transport_ctrl;     /* 0x04: Transport control */
    uint8_t     packet_type;        /* 0x05: Packet type (1 = RIP) */
    uint32_t    dest_network;       /* 0x06: Destination network */
    uint8_t     dest_host[6];       /* 0x0A: Destination host (broadcast) */
    uint16_t    dest_socket;        /* 0x10: Destination socket */
    uint32_t    src_network;        /* 0x12: Source network */
    uint8_t     src_host[6];        /* 0x16: Source host */
    uint16_t    src_socket;         /* 0x1C: Source socket (1 = RIP response) */
} rip_$send_hdr_t;

/*
 * XNS broadcast address
 */
static const uint8_t XNS_BROADCAST_HOST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/*
 * =============================================================================
 * RIP_$SEND_TO_PORT
 * =============================================================================
 *
 * Send a RIP packet to a specific port using XNS/IDP.
 *
 * This is a nested Pascal procedure that accesses the caller's stack frame
 * for packet data. In C, we implement it with explicit parameters.
 *
 * The function:
 * 1. Allocates a network buffer header
 * 2. Builds an IDP packet header with broadcast destination
 * 3. Copies the caller's route data into the packet
 * 4. Sends via XNS_IDP_$OS_SEND
 * 5. Returns the buffer
 * 6. Advances the port's event counter if port is active
 *
 * @param port_index    Port index (0-7)
 * @param addr_info     Source address info (12 bytes)
 * @param route_data    Route data buffer
 * @param route_len     Route data length
 *
 * Original address: 0x00E870DC
 */
void RIP_$SEND_TO_PORT(int16_t port_index, void *addr_info,
                        void *route_data, uint16_t route_len)
{
    uint32_t hdr_phys;
    uint32_t hdr_va;
    rip_$send_hdr_t *hdr;
    uint8_t *port_entry;
    status_$t status;

    /* XNS_IDP_$OS_SEND parameters */
    struct {
        uint32_t    hdr_va;         /* -0x2C: Header buffer VA */
        uint8_t     _pad1[0x1C];    /* Padding */
    } send_params;

    uint16_t checksum_ret;

    /* Get a network buffer header */
    NETBUF_$GET_HDR(&hdr_phys, &hdr_va);

    /* Get port entry pointer */
    port_entry = ROUTE_$PORT_ARRAY_BYTES + (port_index * PORT_ENTRY_SIZE);

    /* Build IDP packet header */
    hdr = (rip_$send_hdr_t *)hdr_va;

    /* Checksum = 0xFFFF (no checksum) */
    hdr->checksum = 0xFFFF;

    /* Length = header (0x1E) + route data length */
    hdr->length = route_len;

    /* Transport control = 0, Packet type = 1 (RIP) */
    hdr->transport_ctrl = 0;
    hdr->packet_type = 1;

    /* Destination: Copy from caller's address info (12 bytes = network + host + socket) */
    {
        uint8_t *dest = (uint8_t *)&hdr->dest_network;
        uint8_t *src = (uint8_t *)addr_info;
        int i;
        for (i = 0; i < 12; i++) {
            dest[i] = src[i];
        }
    }

    /* Source: From port entry network and host */
    /* Network at port+0x20, Host at port+0x24 (12 bytes total) */
    {
        uint8_t *dest = (uint8_t *)&hdr->src_network;
        uint8_t *src = port_entry + PORT_ALT_NETWORK_OFF;
        int i;
        for (i = 0; i < 12; i++) {
            dest[i] = src[i];
        }
    }

    /* Source socket = 1 (RIP response socket) */
    hdr->src_socket = 1;

    /* Copy route data after header */
    {
        char *dest = (char *)hdr_va + 0x1E;  /* After 30-byte header */
        OS_$DATA_COPY((char *)route_data, dest, (int32_t)route_len);
    }

    /* Set up send parameters */
    send_params.hdr_va = hdr_va;

    /* Send via XNS_IDP_$OS_SEND */
    XNS_IDP_$OS_SEND(&RIP_$STD_IDP_CHANNEL, &send_params, &checksum_ret, &status);

    /* Return the header buffer */
    NETBUF_$RTN_HDR(&hdr_va);

    /* If port is active (state == 2), advance its event counter */
    if (*(uint16_t *)(port_entry + PORT_STATE_OFF) == PORT_STATE_ACTIVE) {
        EC_$ADVANCE((ec_$eventcount_t *)(port_entry + PORT_EVENTCOUNT_OFF));
    }
}

/*
 * RTWIRED_PROC_START is now implemented in route/rtwired_proc_start.c
 * and declared in route/route_internal.h.
 */

/*
 * =============================================================================
 * RIP_$SEND
 * =============================================================================
 *
 * Main RIP send function - sends RIP packet to one or all ports.
 *
 * This function dispatches to either RIP_$SEND_TO_PORT (for XNS/IDP networks)
 * or RTWIRED_PROC_START (for wired/local networks) based on port flags.
 *
 * @param addr_info     Source address info (12 bytes: network + host + socket)
 * @param port_index    Port index (0-7), or -1 for all ports (broadcast)
 * @param route_data    Route data buffer (cmd + entries)
 * @param route_len     Route data length
 * @param flags         If < 0: use non-standard route ports, send via IDP only
 *                      If >= 0: use standard route ports, get new packet ID first
 *
 * Original address: 0x00E871B6
 */
void RIP_$SEND(void *addr_info, int16_t port_index, void *route_data,
               uint16_t route_len, int8_t flags)
{
    uint8_t *addr_buf = (uint8_t *)addr_info;
    int16_t pkt_id;
    int16_t i;
    uint8_t *port_entry;
    uint16_t port_flags;

    /* If sending standard routes (flags >= 0), get a new packet ID */
    if (flags >= 0) {
        pkt_id = PKT_$NEXT_ID();
    }

    /* Check if sending to all ports or a specific port */
    if (port_index == -1) {
        /*
         * Broadcast to all ports
         *
         * Set up broadcast destination address:
         * - Host = 0xFFFF 0xFFFF 0xFFFF (broadcast)
         * - Socket = 1 (RIP)
         */
        addr_buf[4] = 0xFF;     /* Host byte 0 */
        addr_buf[5] = 0xFF;     /* Host byte 1 */
        addr_buf[6] = 0xFF;     /* Host byte 2 */
        addr_buf[7] = 0xFF;     /* Host byte 3 */
        addr_buf[8] = 0xFF;     /* Host byte 4 */
        addr_buf[9] = 0xFF;     /* Host byte 5 */
        addr_buf[10] = 0x00;    /* Socket high byte */
        addr_buf[11] = 0x01;    /* Socket low byte = 1 */

        /* Iterate through all 8 ports */
        for (i = 0; i < 8; i++) {
            port_entry = ROUTE_$PORT_ARRAY_BYTES + (i * PORT_ENTRY_SIZE);

            /* Copy port's network address to destination */
            *(uint32_t *)addr_buf = *(uint32_t *)port_entry;

            /* Get port flags */
            port_flags = *(uint16_t *)(port_entry + PORT_FLAGS_OFF);

            if (flags < 0) {
                /*
                 * Non-standard routes:
                 * Check if port supports non-standard routing (bits 4,5)
                 */
                if ((1 << (port_flags & 0x1F)) & PORT_FLAG_MASK_NONSTD) {
                    RIP_$SEND_TO_PORT(i, addr_buf, route_data, route_len);
                }
            } else {
                /*
                 * Standard routes:
                 * Check if port supports standard routing (bits 3,5)
                 */
                if ((1 << (port_flags & 0x1F)) & PORT_FLAG_MASK_STD) {
                    /* Use RTWIRED_PROC_START for wired ports */
                    RTWIRED_PROC_START(i, pkt_id, route_data, route_len);
                } else if (flags < 0) {
                    /* Fallback check for non-standard */
                    if ((1 << (port_flags & 0x1F)) & PORT_FLAG_MASK_NONSTD) {
                        RIP_$SEND_TO_PORT(i, addr_buf, route_data, route_len);
                    }
                }
            }
        }
    } else {
        /*
         * Send to specific port
         */
        if (flags < 0) {
            /* Non-standard: always use IDP */
            RIP_$SEND_TO_PORT(port_index, addr_buf, route_data, route_len);
        } else {
            /* Standard: use wired send */
            RTWIRED_PROC_START(port_index, pkt_id, route_data, route_len);
        }
    }
}

/*
 * =============================================================================
 * RIP_$BROADCAST
 * =============================================================================
 *
 * Build and broadcast the full routing table.
 *
 * This function iterates through all routing table entries, builds a RIP
 * response packet containing all valid routes, and sends it to all ports.
 *
 * @param flags     If < 0: broadcast non-standard routes (cap metric at 16)
 *                  If >= 0: broadcast standard routes
 *
 * Packet format:
 * - 2 bytes: Command (2 = response)
 * - N * 6 bytes: Route entries (4 byte network + 2 byte metric)
 *
 * Original address: 0x00E87298
 */
void RIP_$BROADCAST(uint8_t flags)
{
    /* Response buffer - command + up to 90 entries (6 bytes each) */
    uint8_t response_buf[2 + RIP_MAX_ENTRIES * RIP_ENTRY_SIZE];
    uint16_t *cmd_ptr;
    int16_t entry_count;
    int16_t i;
    rip_$entry_t *entry;
    rip_$route_t *route;
    uint8_t state;
    uint8_t *port_entry;
    uint16_t port_flags;
    uint16_t metric;
    int route_offset;

    /* Broadcast address buffer: network + host + socket */
    uint8_t addr_buf[12];

    /* Set response command = 2 */
    cmd_ptr = (uint16_t *)response_buf;
    *cmd_ptr = 2;

    entry_count = 0;

    /* Iterate through all 64 routing table entries */
    for (i = 0; i < RIP_TABLE_SIZE; i++) {
        entry = &RIP_$INFO[i];

        /* Select route based on flags */
        if ((int8_t)flags < 0) {
            /* Non-standard route at entry+0x18 */
            route = &entry->routes[1];
        } else {
            /* Standard route at entry+0x04 */
            route = &entry->routes[0];
        }

        /* Check if route is valid (state VALID or AGING) */
        state = (route->flags >> RIP_STATE_SHIFT) & 0x03;
        if (state == RIP_STATE_UNUSED || state == RIP_STATE_EXPIRED) {
            continue;
        }

        /* Get the port this route uses */
        port_entry = ROUTE_$PORT_ARRAY_BYTES + (route->port * PORT_ENTRY_SIZE);
        port_flags = *(uint16_t *)(port_entry + PORT_FLAGS_OFF);

        /* Check if this port should be included based on flags */
        if ((int8_t)flags < 0) {
            /* Non-standard: check bits 4,5 */
            if (((1 << (port_flags & 0x1F)) & PORT_FLAG_MASK_NONSTD) == 0) {
                /* Not a non-standard port - check standard bits too */
                if ((int8_t)flags >= 0) {
                    continue;
                }
                if (((1 << (port_flags & 0x1F)) & PORT_FLAG_MASK_STD) == 0) {
                    continue;
                }
            }
        } else {
            /* Standard: check bits 3,5 */
            if (((1 << (port_flags & 0x1F)) & PORT_FLAG_MASK_STD) == 0) {
                continue;
            }
        }

        /* Add entry to response buffer */
        entry_count++;
        route_offset = entry_count * 6;  /* 6 bytes per entry, starting after cmd */

        /* Store network address (4 bytes) */
        *(uint32_t *)(response_buf + route_offset - 4) = entry->network;

        /* Calculate and store metric (2 bytes) */
        if ((int8_t)flags < 0) {
            /* Non-standard: metric + 1, capped at 16 */
            metric = route->metric + 1;
            if (metric > 0x10) {
                metric = 0x10;
            }
        } else {
            /* Standard: metric + 1 */
            metric = route->metric + 1;
        }
        *(uint16_t *)(response_buf + route_offset - 2 + 4) = metric;

        /* Maximum 90 entries per packet */
        if (entry_count >= RIP_MAX_ENTRIES) {
            break;
        }
    }

    /* If we have entries, send the packet */
    if (entry_count > 0) {
        /* Calculate total packet length: cmd (2 bytes) + entries (6 bytes each) */
        uint16_t packet_len = entry_count * 6 + 2;

        /* Clear address buffer */
        for (i = 0; i < 12; i++) {
            addr_buf[i] = 0;
        }

        /* Send to all ports (port_index = -1) */
        RIP_$SEND(addr_buf, -1, response_buf, packet_len, flags);
    }
}
