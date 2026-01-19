/*
 * RIP_$SERVER - RIP Protocol Server Implementation
 *
 * This file implements the RIP (Routing Information Protocol) server functions
 * that handle incoming routing protocol packets and send responses.
 *
 * Functions implemented:
 * - RIP_$PACKET_LENGTH: Calculate RIP packet data length
 * - RIP_$SEND_UPDATES: Send routing updates if changes detected
 * - RIP_$PROCESS_REQUEST: Build response to RIP request
 * - RIP_$SERVER: Main server - process incoming RIP packets
 *
 * Original addresses:
 * - RIP_$PACKET_LENGTH:    0x00E68864
 * - RIP_$SEND_UPDATES:     0x00E6887A
 * - RIP_$PROCESS_REQUEST:  0x00E688C8
 * - RIP_$SERVER:           0x00E68A08
 */

#include "rip/rip_internal.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"
#include "time/time.h"
#include "rem_name/rem_name.h"
#include "hint/hint.h"

/*
 * =============================================================================
 * RIP Packet Format
 * =============================================================================
 *
 * RIP packets consist of:
 * - 2 byte command (1=request, 2=response, 3=name service registration)
 * - N entries, each 6 bytes:
 *   - 4 byte network address
 *   - 2 byte metric (hop count)
 *
 * Maximum 90 (0x5A) entries per packet.
 */

/* RIP command types */
#define RIP_CMD_REQUEST         1
#define RIP_CMD_RESPONSE        2
#define RIP_CMD_NAME_REGISTER   3

/* Maximum entries per RIP packet */
#define RIP_MAX_ENTRIES         0x5A    /* 90 entries */

/* RIP packet entry size */
#define RIP_ENTRY_SIZE          6

/* RIP socket number */
#define RIP_SOCKET              8

/* Retry count for RIP_$SEND responses */
#define RIP_SEND_RETRIES        5

/* Retry timeout in 100us units (25000 = 2.5 seconds) */
#define RIP_SEND_TIMEOUT        25000

/*
 * =============================================================================
 * RIP Statistics Structure
 * =============================================================================
 */

/*
 * RIP_$STATS - RIP protocol statistics
 *
 * Located at 0xE262AC, tracks packet processing statistics.
 */
typedef struct rip_$stats_t {
    uint16_t    _reserved0;         /* 0x00: Reserved */
    uint32_t    packets_received;   /* 0x02: Total packets received */
    uint16_t    _reserved1;         /* 0x06: Reserved */
    uint32_t    errors;             /* 0x08: Packet errors */
    uint16_t    unknown_commands;   /* 0x0C: Unknown command types */
    /* ... more fields follow */
} rip_$stats_t;

#if defined(M68K)
    #define RIP_$STATS              (*(rip_$stats_t *)0xE262AC)
    #define ROUTE_$STD_N_ROUTING_PORTS  (*(int16_t *)0xE26F1A)
    #define ROUTE_$N_ROUTING_PORTS      (*(int16_t *)0xE26F1C)

    /* Recent changes flags (signed bytes - negative means changes pending) */
    #define RIP_$STD_RECENT_CHANGES     (*(int8_t *)0xE26EDE)
    #define RIP_$RECENT_CHANGES         (*(int8_t *)0xE26EE0)

    /* Response timer eventcount (embedded in code at 0xE68E26) */
    #define RIP_$RESPONSE_TIMER         (*(ec_$eventcount_t *)0xE68E26)
#else
    extern rip_$stats_t RIP_$STATS;
    extern int16_t ROUTE_$STD_N_ROUTING_PORTS;
    extern int16_t ROUTE_$N_ROUTING_PORTS;
    extern int8_t RIP_$STD_RECENT_CHANGES;
    extern int8_t RIP_$RECENT_CHANGES;
    extern ec_$eventcount_t RIP_$RESPONSE_TIMER;
#endif

/*
 * =============================================================================
 * External Function Prototypes
 * =============================================================================
 */

/* Packet functions */
extern void PKT_$DUMP_DATA(void *header, uint16_t length);
extern void PKT_$BRK_INTERNET_HDR(void *packet, int32_t *src_network,
                                   uint8_t *src_info, uint8_t *dest_info,
                                   uint32_t *idp_network, uint32_t *idp_host,
                                   uint16_t *port_network, uint16_t *unused1,
                                   uint16_t *port_socket, uint16_t *data,
                                   uint16_t max_data, int16_t *data_len,
                                   status_$t *status);
extern void PKT_$SEND_INTERNET(uint32_t idp_network, uint32_t idp_host,
                                uint16_t port_network, int32_t src_network,
                                uint32_t node_id, uint16_t socket,
                                void *data, uint16_t port_socket,
                                void *route_data, uint16_t route_len,
                                uint32_t callback, uint16_t flags,
                                void *extra1, uint32_t extra2,
                                status_$t *status);

/* Socket functions */
extern uint16_t SOCK_$GET(uint16_t socket, void **packet_ret);

/* Network buffer functions */
extern uint16_t NETBUF_$RTN_HDR(void **packet);

/* Broadcast function (implemented in broadcast.c) */
extern void RIP_$BROADCAST(uint8_t flags);

/* Send function (implemented in send.c) */
extern void RIP_$SEND(void *addr_info, int16_t port, void *route_data,
                      uint16_t route_len, int8_t flags);

/* Name service registration */
extern uint16_t REM_NAME_$REGISTER_SERVER(void *param1, void *param2);

/* Timer wait */
extern uint16_t TIME_$WAIT(ec_$eventcount_t *event, uint32_t *timeout,
                           status_$t *status);

/* Network hint */
extern void HINT_$ADD_NET(int16_t network);

/* Wired routing start */
extern void RTWIRED_PROC_START(int16_t port);

/*
 * =============================================================================
 * RIP_$PACKET_LENGTH
 * =============================================================================
 *
 * Calculate RIP packet data length from entry count.
 *
 * @param entry_count   Number of route entries
 * @return              Packet data length in bytes (entry_count * 6 + 2)
 *
 * Original address: 0x00E68864
 */
int16_t RIP_$PACKET_LENGTH(int16_t entry_count)
{
    /* Each entry is 6 bytes (4 byte network + 2 byte metric) */
    /* Plus 2 bytes for command */
    return entry_count * RIP_ENTRY_SIZE + 2;
}

/*
 * =============================================================================
 * RIP_$SEND_UPDATES
 * =============================================================================
 *
 * Send routing updates if there are recent changes.
 *
 * Checks the recent_changes flag for the specified route type.
 * If changes are pending (flag is negative), clears the flag and
 * broadcasts the routing table.
 *
 * @param is_std    If negative, handle non-standard routes; else standard routes
 *
 * Original address: 0x00E6887A
 */
void RIP_$SEND_UPDATES(int16_t is_std)
{
    uint8_t flags;

    if ((int8_t)is_std < 0) {
        /* Non-standard routes */
        if (ROUTE_$STD_N_ROUTING_PORTS < 2) {
            /* Not enough ports for routing */
            return;
        }
        if (RIP_$STD_RECENT_CHANGES >= 0) {
            /* No recent changes */
            return;
        }
        RIP_$STD_RECENT_CHANGES = 0;
        flags = 0xFF;
    } else {
        /* Standard routes */
        if (ROUTE_$N_ROUTING_PORTS < 2) {
            /* Not enough ports for routing */
            return;
        }
        if (RIP_$RECENT_CHANGES >= 0) {
            /* No recent changes */
            return;
        }
        RIP_$RECENT_CHANGES = 0;
        flags = 0;
    }

    RIP_$BROADCAST(flags);
}

/*
 * =============================================================================
 * RIP_$PROCESS_REQUEST
 * =============================================================================
 *
 * Process a RIP request packet and build the response data.
 *
 * This function is called from RIP_$SERVER when a RIP request (command=1)
 * is received. It builds a response containing the requested route information.
 *
 * Two modes of operation:
 * 1. Specific networks: Request lists specific network addresses to query
 * 2. Full table: Request contains network=0xFFFFFFFF, returns all valid routes
 *
 * The response is built in the stack frame of the caller (RIP_$SERVER):
 * - response_count at frame - 0x512
 * - response_data at frame - 0x294 (6 bytes per entry)
 *
 * @param flags     If negative, use non-standard routes; else standard routes
 *
 * Stack frame layout (caller's frame in A6):
 *   -0x514: request entry count (input)
 *   -0x512: response entry count (output)
 *   -0x4f0: pointer to request packet data
 *   -0x294: response buffer start (6 bytes per entry: 4 byte net + 2 byte metric)
 *
 * Original address: 0x00E688C8
 */

/* This function operates on the caller's stack frame, making it a nested procedure */
/* We implement it using the caller's frame pointer passed implicitly */

static void RIP_$PROCESS_REQUEST_INTERNAL(int8_t flags, uint8_t *frame_ptr)
{
    int16_t request_count;
    int16_t response_count;
    uint32_t *request_data;
    uint8_t *response_ptr;
    int16_t i;
    rip_$entry_t *entry;
    rip_$route_t *route;
    uint16_t metric;
    int full_table_request;

    /*
     * Stack frame offsets (relative to caller's A6):
     * -0x514: request_count
     * -0x512: response_count
     * -0x4f0: request_data pointer
     * -0x294: response buffer
     * -0x290: first entry's metric (at offset 4 into entry)
     */

    request_count = *(int16_t *)(frame_ptr - 0x514);
    request_data = *(uint32_t **)(frame_ptr - 0x4f0);
    response_ptr = frame_ptr - 0x294;

    /* Initialize response: command = 2 (response) */
    *(uint16_t *)(frame_ptr - 0x290) = 2;

    /* Copy request count to response count initially */
    *(int16_t *)(frame_ptr - 0x512) = request_count;

    /* Check for empty request */
    if (request_count <= 0) {
        return;
    }

    full_table_request = 0;

    /* Process each requested network */
    for (i = 0; i < request_count; i++) {
        uint32_t network = request_data[i];  /* Networks start at offset 2 in request */

        if (network == 0xFFFFFFFF) {
            /* Full table request - will enumerate all routes below */
            full_table_request = 1;
            break;
        }

        /* Look up specific network */
        entry = RIP_$NET_LOOKUP(network, 0, 0);

        /* Store network in response */
        *(uint32_t *)(response_ptr + i * 6) = network;

        if (flags < 0) {
            /* Non-standard routes at entry + 0x18 */
            if (entry == NULL) {
                metric = 0x10;  /* Unreachable */
            } else {
                metric = entry->routes[1].metric + 1;
                if (metric < 0x10) {
                    metric = 0x10;  /* Clamp to unreachable */
                }
            }
        } else {
            /* Standard routes at entry + 0x04 */
            if (entry == NULL) {
                metric = 0x11;  /* Unreachable (infinity) */
            } else {
                metric = entry->routes[0].metric + 1;
            }
        }

        /* Store metric in response */
        *(uint16_t *)(response_ptr + i * 6 + 4) = metric;
    }

    /* Handle full table request */
    if (full_table_request) {
        response_count = 0;
        *(int16_t *)(frame_ptr - 0x512) = 0;

        /* Enumerate all routing table entries */
        for (i = 0; i < RIP_TABLE_SIZE; i++) {
            entry = &RIP_$INFO[i];

            if (flags < 0) {
                route = &entry->routes[1];  /* Non-standard */
            } else {
                route = &entry->routes[0];  /* Standard */
            }

            /* Check if route is VALID (1) or AGING (2) */
            uint8_t state = (route->flags >> RIP_STATE_SHIFT) & 0x03;
            if (state == RIP_STATE_VALID || state == RIP_STATE_AGING) {
                response_count++;
                *(int16_t *)(frame_ptr - 0x512) = response_count;

                /* Store network */
                *(uint32_t *)(response_ptr + response_count * 6 - 6) = entry->network;

                /* Calculate and store metric */
                if (flags < 0) {
                    metric = route->metric + 1;
                    if (metric < 0x10) {
                        metric = 0x10;
                    }
                } else {
                    metric = route->metric + 1;
                }
                *(uint16_t *)(response_ptr + response_count * 6 - 2) = metric;

                /* Maximum 90 entries in response */
                if (response_count >= RIP_MAX_ENTRIES) {
                    return;
                }
            }
        }
    }
}

/*
 * Public wrapper - this is what gets called
 * In the original code, this accesses the caller's stack frame via A6
 */
void RIP_$PROCESS_REQUEST(int8_t flags)
{
    /*
     * Note: In the original m68k code, this function accesses the caller's
     * stack frame directly using register A6. This C implementation would
     * need to be called with the frame pointer, or the caller would need
     * to pass the necessary buffers explicitly.
     *
     * For now, this serves as documentation of the algorithm.
     * A proper implementation would require restructuring RIP_$SERVER
     * to pass buffers explicitly.
     */

    /* This cannot be properly implemented in standard C without access */
    /* to the caller's stack frame. See RIP_$SERVER for the integrated version. */
}

/*
 * =============================================================================
 * RIP_$SERVER
 * =============================================================================
 *
 * Main RIP server function - processes incoming RIP packets.
 *
 * Handles three types of RIP packets:
 * 1. Request (cmd=1): Send back routing information for requested networks
 * 2. Response (cmd=2): Update routing table with received routes
 * 3. Name register (cmd=3): Register name service (special Apollo extension)
 *
 * Called from socket receive processing when a packet arrives on socket 8.
 *
 * Original address: 0x00E68A08
 */
uint16_t RIP_$SERVER(void)
{
    void *packet;
    uint16_t result;
    int8_t is_std;
    uint8_t packet_flags;

    /* Local buffers for packet parsing */
    int32_t src_network;
    uint8_t src_info[4];
    uint8_t dest_info[2];
    uint32_t idp_network;
    uint32_t idp_host;
    uint16_t port_network;
    uint16_t unused1;
    uint16_t port_socket;
    int16_t data_len;
    status_$t status;

    /* Packet header copy for non-standard processing */
    uint8_t header_copy[0x1E];

    /* Request/response data buffers */
    uint16_t packet_data[0x10F];  /* Up to 90 entries + command */
    int16_t entry_count;

    /* Response buffer */
    uint16_t response_cmd;
    int16_t response_count;
    uint8_t response_data[RIP_MAX_ENTRIES * RIP_ENTRY_SIZE];

    /* Source address for updates */
    rip_$xns_addr_t source_addr;

    int16_t port_index;
    route_$port_t *port;
    uint16_t i;

    /* Get packet from socket 8 (RIP socket) */
    result = SOCK_$GET(RIP_SOCKET, &packet);
    if ((int8_t)result >= 0) {
        /* No packet available */
        return result;
    }

    /* Check packet flags for standard vs non-standard */
    packet_flags = *((uint8_t *)packet + 0x41);  /* Flags at offset 0x41 from packet base */
    is_std = (packet_flags & 0x02) ? -1 : 0;

    /* Dump packet for debugging */
    PKT_$DUMP_DATA((uint8_t *)packet + 0x10, *((uint16_t *)packet + 0x23));

    if (is_std < 0) {
        /* Non-standard packet - copy header and data directly */
        status = 0;

        /* Copy header (0x1E bytes) */
        for (i = 0; i < 0x1E; i++) {
            header_copy[i] = ((uint8_t *)packet)[i];
        }

        /* Copy packet data */
        uint8_t *pkt_data = (uint8_t *)packet + 0x1E;
        for (i = 0; i < 0x10E; i++) {
            ((uint8_t *)packet_data)[i] = pkt_data[i];
        }

        src_network = -1;  /* Unknown source network for non-standard */
        data_len = *((uint16_t *)header_copy + 0x0F) - 0x1E;  /* Subtract header size */
    } else {
        /* Standard packet - parse IDP header */
        PKT_$BRK_INTERNET_HDR(packet, &src_network, src_info, dest_info,
                              &idp_network, &idp_host, &port_network,
                              &unused1, &port_socket, packet_data,
                              0x21E, &data_len, &status);
    }

    /* Update statistics - packet received */
    RIP_$STATS.packets_received++;

    /* Validate packet */
    if (status != 0) {
        goto error_return;
    }

    /* Calculate entry count: (data_len - 2) / 6 */
    entry_count = (data_len - 2) / RIP_ENTRY_SIZE;

    if (entry_count < 0 || entry_count > RIP_MAX_ENTRIES) {
        goto error_return;
    }

    /* Verify packet length matches entry count */
    if (RIP_$PACKET_LENGTH(entry_count) != data_len) {
        goto error_return;
    }

    /* Find port for this packet */
    {
        uint32_t page_base = (uint32_t)packet & 0xFFFFFC00;
        port_network = *(uint16_t *)(page_base + 0x3E0);
        uint32_t port_sock = *(uint16_t *)(page_base + 0x3E2);
        port_index = ROUTE_$FIND_PORT(port_network, port_sock);
    }

    /* Return packet buffer */
    {
        void *pkt_ptr = packet;
        result = NETBUF_$RTN_HDR(&pkt_ptr);
    }

    if (port_index == -1) {
        /* Unknown port - ignore packet */
        return result;
    }

    /* Dispatch based on command type */
    uint16_t command = packet_data[0];

    switch (command) {
    case RIP_CMD_REQUEST:
        /*
         * RIP Request - send back routing information
         */
        if (is_std < 0) {
            /* Non-standard request */
            if (ROUTE_$STD_N_ROUTING_PORTS < 2) {
                /* Check if this is a broadcast request (all FFs in address) */
                uint16_t *addr = (uint16_t *)&header_copy[0x14];
                if (addr[0] == 0xFFFF && addr[1] == 0xFFFF && addr[2] == 0xFFFF) {
                    return 0xFF;
                }
            }

            /* Process request and build response in local buffer */
            /* Note: Original uses nested procedure accessing caller's frame */
            response_count = 0;
            response_cmd = RIP_CMD_RESPONSE;

            /* Build response for non-standard routes */
            for (i = 0; i < (uint16_t)entry_count; i++) {
                uint32_t net = *(uint32_t *)&packet_data[1 + i * 3];
                rip_$entry_t *entry;
                uint16_t metric;

                if (net == 0xFFFFFFFF) {
                    /* Full table request - enumerate all routes */
                    int j;
                    for (j = 0; j < RIP_TABLE_SIZE && response_count < RIP_MAX_ENTRIES; j++) {
                        entry = &RIP_$INFO[j];
                        rip_$route_t *route = &entry->routes[1];
                        uint8_t state = (route->flags >> RIP_STATE_SHIFT) & 0x03;

                        if (state == RIP_STATE_VALID || state == RIP_STATE_AGING) {
                            *(uint32_t *)&response_data[response_count * 6] = entry->network;
                            metric = route->metric + 1;
                            if (metric < 0x10) metric = 0x10;
                            *(uint16_t *)&response_data[response_count * 6 + 4] = metric;
                            response_count++;
                        }
                    }
                    break;
                }

                entry = RIP_$NET_LOOKUP(net, 0, 0);
                *(uint32_t *)&response_data[response_count * 6] = net;

                if (entry == NULL) {
                    metric = 0x10;
                } else {
                    metric = entry->routes[1].metric + 1;
                    if (metric < 0x10) metric = 0x10;
                }
                *(uint16_t *)&response_data[response_count * 6 + 4] = metric;
                response_count++;
            }

            /* Send response with retry loop */
            {
                uint8_t xns_addr[12];
                /* Copy from header - source becomes destination */
                for (i = 0; i < 12; i++) {
                    xns_addr[i] = header_copy[0x08 + i];  /* Source address in header */
                }
                *(uint16_t *)&xns_addr[10] = 1;  /* Socket 1? */

                for (i = 0; i < RIP_SEND_RETRIES; i++) {
                    RIP_$SEND(xns_addr, port_index, response_data,
                              RIP_$PACKET_LENGTH(response_count), 0xFF);

                    /* Wait for response or timeout */
                    uint32_t timeout_val = 0;
                    uint16_t timeout_msec = RIP_SEND_TIMEOUT;
                    status_$t wait_status;

                    result = TIME_$WAIT(&RIP_$RESPONSE_TIMER, &timeout_val, &wait_status);

                    if (wait_status == 0xD0003) {
                        /* Timeout - done */
                        return result;
                    }
                }
            }
            return result;
        } else {
            /* Standard request */
            if (ROUTE_$N_ROUTING_PORTS < 2) {
                uint8_t *response_flags = (uint8_t *)&packet_data[0x155];
                if ((int8_t)*response_flags < 0) {
                    return 0xFF;
                }
            }

            /* Build response for standard routes */
            response_count = 0;
            response_cmd = RIP_CMD_RESPONSE;

            for (i = 0; i < (uint16_t)entry_count; i++) {
                uint32_t net = *(uint32_t *)&packet_data[1 + i * 3];
                rip_$entry_t *entry;
                uint16_t metric;

                if (net == 0xFFFFFFFF) {
                    /* Full table request */
                    int j;
                    for (j = 0; j < RIP_TABLE_SIZE && response_count < RIP_MAX_ENTRIES; j++) {
                        entry = &RIP_$INFO[j];
                        rip_$route_t *route = &entry->routes[0];
                        uint8_t state = (route->flags >> RIP_STATE_SHIFT) & 0x03;

                        if (state == RIP_STATE_VALID || state == RIP_STATE_AGING) {
                            *(uint32_t *)&response_data[response_count * 6] = entry->network;
                            metric = route->metric + 1;
                            *(uint16_t *)&response_data[response_count * 6 + 4] = metric;
                            response_count++;
                        }
                    }
                    break;
                }

                entry = RIP_$NET_LOOKUP(net, 0, 0);
                *(uint32_t *)&response_data[response_count * 6] = net;

                if (entry == NULL) {
                    metric = RIP_INFINITY;
                } else {
                    metric = entry->routes[0].metric + 1;
                }
                *(uint16_t *)&response_data[response_count * 6 + 4] = metric;
                response_count++;
            }

            /* Set response command */
            response_cmd = 0x20;  /* Response with extended flag? */

            /* Send via PKT_$SEND_INTERNET for standard packets */
            result = (uint16_t)PKT_$SEND_INTERNET(idp_network, idp_host, port_network,
                                                   src_network, NODE_$ME, RIP_SOCKET,
                                                   &response_cmd, port_socket,
                                                   response_data, RIP_$PACKET_LENGTH(response_count),
                                                   0x00E68E28, 0,  /* Callback address */
                                                   dest_info, 0, &status);
            return result;
        }
        break;

    case RIP_CMD_RESPONSE:
        /*
         * RIP Response - update routing table
         */
        port = ROUTE_$PORTP[port_index];

        /* Get source network from packet or header */
        if (is_std < 0) {
            src_network = *(int32_t *)&header_copy[0x1A];
        }

        /* Check if source network changed for this port */
        if (src_network != *(int32_t *)port) {
            /* Check port flags to see if we should accept this */
            uint16_t port_flags = *(uint16_t *)((uint8_t *)port + 0x2C);
            if ((1 << (port_flags & 0x1F) & 0x38) == 0) {
                /* Port network changed - update routing */
                int32_t old_network = *(int32_t *)port;

                /* Clear source address */
                source_addr.network = old_network;
                source_addr.host[0] = 0;
                source_addr.host[1] = 0;
                source_addr.host[2] = 0;
                source_addr.host[3] = 0;
                source_addr.host[4] = 0;
                source_addr.host[5] = 0;

                /* Invalidate old network route */
                RIP_$UPDATE_INT(old_network, &source_addr, 0x10, port_index,
                                is_std, &status);

                /* Add new network route */
                source_addr.network = src_network;
                RIP_$UPDATE_INT(src_network, &source_addr, 0, port_index,
                                is_std, &status);

                /* Update port network */
                *(int32_t *)port = src_network;
                *(int32_t *)((uint8_t *)port + 0x20) = src_network;

                /* If this is port 0, add to hints */
                if (port_index == 0) {
                    HINT_$ADD_NET((int16_t)*(int32_t *)port);
                }
            }
        }

        /* Check if we should process routes from this packet */
        if (is_std < 0) {
            if (ROUTE_$STD_N_ROUTING_PORTS >= 2) {
                uint16_t port_flags = *(uint16_t *)((uint8_t *)port + 0x2C);
                if (ROUTE_$STD_N_ROUTING_PORTS >= 2 &&
                    (1 << (port_flags & 0x1F) & 0x30) == 0) {
                    goto process_routes;
                }
            }
        } else {
            if (ROUTE_$N_ROUTING_PORTS >= 2) {
                uint16_t port_flags = *(uint16_t *)((uint8_t *)port + 0x2C);
                if (ROUTE_$N_ROUTING_PORTS >= 2 &&
                    (1 << (port_flags & 0x1F) & 0x28) == 0) {
                    goto process_routes;
                }
            }
        }

        /* Don't process individual routes - just send updates if needed */
        goto send_updates;

    process_routes:
        /* Build source address for updates */
        source_addr.network = src_network;
        if (is_std < 0) {
            /* Copy host from header for non-standard */
            source_addr.host[0] = header_copy[0x0A];
            source_addr.host[1] = header_copy[0x0B];
            source_addr.host[2] = header_copy[0x0C];
            source_addr.host[3] = header_copy[0x0D];
            source_addr.host[4] = header_copy[0x0E];
            source_addr.host[5] = header_copy[0x0F];
        } else {
            /* For standard, use idp_host (lower 20 bits) */
            source_addr.host[0] = 0;
            source_addr.host[1] = 0;
            source_addr.host[2] = (idp_host >> 16) & 0x0F;  /* Mask to 20 bits */
            source_addr.host[3] = (idp_host >> 8) & 0xFF;
            source_addr.host[4] = idp_host & 0xFF;
            source_addr.host[5] = 0;
        }

        /* Process each route entry */
        for (i = 0; i < (uint16_t)(entry_count - 1); i++) {
            uint32_t net = *(uint32_t *)&packet_data[1 + i * 3];
            uint16_t metric = packet_data[3 + i * 3];

            RIP_$UPDATE_INT(net, &source_addr, metric, port_index, is_std, &status);
        }

    send_updates:
        /* Send any pending updates */
        result = RIP_$SEND_UPDATES(is_std);
        return result;

    case RIP_CMD_NAME_REGISTER:
        /*
         * Name service registration (Apollo extension)
         */
        if (is_std < 0) {
            /* Non-standard - check for specific socket type */
            if ((uint8_t)header_copy[0x1B] != 0xBE) {
                RIP_$STATS.unknown_commands++;
                return 3;
            }

            /* Extract parameters and call name registration */
            uint32_t param1 = *(uint32_t *)&header_copy[0x14];
            uint32_t param2 = *(uint32_t *)&header_copy[0x04] & 0xFFFFF;
            result = REM_NAME_$REGISTER_SERVER(&param1, &param2);
        } else {
            /* Standard - call with IDP parameters */
            result = REM_NAME_$REGISTER_SERVER(&idp_network, &idp_host);
        }
        return result;

    default:
        /* Unknown command */
        RIP_$STATS.unknown_commands++;
        return command;
    }

    return result;

error_return:
    /* Error - return packet and increment error counter */
    RIP_$STATS.errors++;
    {
        void *pkt_ptr = packet;
        result = NETBUF_$RTN_HDR(&pkt_ptr);
    }
    return result;
}
