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
#include "xns/xns.h"     /* xns_$idp_header_t: the IDP header RIP_$SERVER copies */

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
 * RIP_$STATS (0xE262AC), RIP_$STD_RECENT_CHANGES (0xE26EDE),
 * RIP_$RECENT_CHANGES (0xE26EE0) and RIP_$INFO come from rip/rip_internal.h;
 * ROUTE_$STD_N_ROUTING_PORTS / ROUTE_$N_ROUTING_PORTS from route/route.h.
 */

/*
 * Note: Most external function prototypes come from included headers.
 *
 * TODO(source-6sz): The following functions have signature discrepancies between
 * this decompiled code and the headers. These need further analysis
 * to determine the correct signatures.
 */

/*
 * REM_NAME_$REGISTER_SERVER (0xE4A4AE) takes no parameters: the routine only
 * stamps TIME_$CLOCKH into the name server record and sets the "server
 * contacted" flag; it never reads its stack arguments.  The callers push
 * two (ignored) arguments, which is why the decompiler showed parameters.
 */

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
 * @param is_xns    If negative, handle non-standard routes; else standard routes
 *
 * Original address: 0x00E6887A
 */
void RIP_$SEND_UPDATES(boolean is_xns)
{
    uint8_t flags;

    if ((int8_t)is_xns < 0) {
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
void RIP_$PROCESS_REQUEST(boolean flags)
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
 * Frame (link.w A6,-0x538 at 0x00E68A08).  Every displacement the prologue
 * and the PKT_$BRK_INTERNET_HDR call use, with the local it names:
 *
 *   A6-0x070  sock_$pkt_info_t  the record SOCK_$GET fills in (0x40 bytes);
 *                               -0x70 is .hdr, -0x5F the low byte of .flags,
 *                               -0x46 .data_len, -0x44 .hdr_len, -0x40
 *                               .data_pages
 *   A6-0x020  xns_$idp_header_t the 30-byte IDP header copy the XNS path
 *                               makes (0x00E68A60-0x00E68A6E); -0x1E is its
 *                               .length and -0x1A its .dest_network
 *   A6-0x4F0  uint32_t          packet + 0x1E, the XNS payload address
 *   A6-0x4FC  uint32_t          BRK arg 3,  routing_key
 *   A6-0x500  uint32_t          BRK arg 4,  dest_node
 *   A6-0x51C  uint16_t          BRK arg 5,  dest_sock
 *   A6-0x4F4  uint32_t          BRK arg 6,  src_node_or
 *   A6-0x4F8  uint32_t          BRK arg 7,  src_node
 *   A6-0x51A  uint16_t          BRK arg 8,  src_sock
 *   A6-0x2B0  pkt info record   BRK arg 9,  info_out
 *   A6-0x518  uint16_t          BRK arg 10, id_out
 *   A6-0x4D0  uint8_t[0x21E]    BRK arg 11, the payload buffer
 *   A6-0x516  uint16_t          BRK arg 13, the payload length
 *   A6-0x4EC  status_$t         BRK arg 14, status_ret
 *   A6-0x514  int16_t           the entry count (data_len - 2) / 6
 *   A6-0x508  uint32_t          the header VA handed to NETBUF_$RTN_HDR
 *
 * Original address: 0x00E68A08
 */
void RIP_$SERVER(void)
{
    sock_$pkt_info_t    pkt;            /* A6-0x70 */
    xns_$idp_header_t  *packet;         /* A2 = pkt.hdr */
    uint8_t            *page_base;      /* A3 = A2 & ~0x3FF, the netbuf page */
    boolean             got_packet;     /* D0b */
    boolean             is_xns;         /* D3b */

    /* PKT_$BRK_INTERNET_HDR's output block */
    uint32_t    routing_key;            /* A6-0x4FC */
    uint32_t    dest_node;              /* A6-0x500 */
    uint16_t    dest_sock;              /* A6-0x51C */
    uint32_t    src_node_or;            /* A6-0x4F4 */
    uint32_t    src_node;               /* A6-0x4F8 */
    uint16_t    src_sock;               /* A6-0x51A */
    uint16_t    info_out[15];           /* A6-0x2B0: the 30-byte info record */
    uint16_t    id_out;                 /* A6-0x518 */
    uint16_t    data_len;               /* A6-0x516 */
    status_$t   status;                 /* A6-0x4EC */

    uint32_t    payload_va;             /* A6-0x4F0 */
    uint16_t    packet_data[0x10F];     /* A6-0x4D0, 0x21E bytes */
    xns_$idp_header_t header_copy;      /* A6-0x20 */
    uint8_t    *header_bytes = (uint8_t *)&header_copy;
    int16_t     entry_count;            /* A6-0x514 */
    uint32_t    hdr_va;                 /* A6-0x508 */

    int16_t     port_index;
    uint16_t    i;

    /*
     * TODO(source-4nvz): the three dispatch arms below are still the earlier
     * sketch.  They have NOT been traced against 0x00E68B5E-0x00E68E1C, so
     * the locals they use keep their old names and their reads of
     * header_bytes[] / packet_data[] are unverified.  The head of the
     * function (0x00E68A08-0x00E68B5C) is a faithful translation.
     */
    uint16_t    port_network;
    uint16_t    port_socket;
    uint32_t    idp_network;
    uint32_t    idp_host;
    int32_t     src_network;
    route_$port_t *port;
    rip_$xns_addr_t source_addr;

    /* Response buffer built by the request arm */
    uint16_t response_cmd;
    int16_t response_count;
    uint8_t response_data[RIP_MAX_ENTRIES * RIP_ENTRY_SIZE];

    /* 0x00E68A10-0x00E68A24: SOCK_$GET(8, &pkt) returns a Pascal boolean */
    got_packet = (boolean)SOCK_$GET(RIP_SOCKET, &pkt);
    if (got_packet >= 0) {
        return;                             /* 0x00E68E1C */
    }

    /*
     * 0x00E68A28: "btst.b #0x1,(-0x5f,A6)" is bit 1 of the LOW byte of
     * sock_$pkt_info_t.flags, i.e. bit 1 of the word - the packet arrived
     * over XNS routing and its IDP header is already in front of us.
     */
    is_xns = (pkt.flags & SOCK_PKT_FLAG_XNS) ? true : false;

    /* 0x00E68A30-0x00E68A40 */
    PKT_$DUMP_DATA(pkt.data_pages, (int16_t)pkt.data_len);

    /* 0x00E68A42-0x00E68A4C */
    packet = (xns_$idp_header_t *)pkt.hdr;
    page_base = (uint8_t *)((uintptr_t)packet & ~(uintptr_t)0x3FF);

    if (is_xns < 0) {
        /* 0x00E68A52-0x00E68A88: no Apollo internet header to parse */
        status = status_$ok;
        payload_va = (uint32_t)(uintptr_t)packet + XNS_IDP_HEADER_SIZE;

        /* seven longwords plus a word: the 30-byte IDP header */
        header_copy = *packet;

        /* 0x87 longwords plus a word: 0x21E bytes of payload */
        for (i = 0; i < sizeof(packet_data); i++) {
            ((uint8_t *)packet_data)[i] =
                ((const uint8_t *)(uintptr_t)payload_va)[i];
        }

        /* 0x00E68A84: the IDP length minus the header it counts */
        data_len = (uint16_t)(header_copy.length - XNS_IDP_HEADER_SIZE);
    } else {
        /*
         * 0x00E68A8E-0x00E68AC4: fourteen arguments, 0x34 bytes of caller
         * cleanup.  Argument 2 is sock_$pkt_info_t.hdr_len, which
         * PKT_$BRK_INTERNET_HDR never reads (nothing in 0x00E12328-0x00E1248C
         * touches (0xC,A6)); it is passed all the same.
         */
        PKT_$BRK_INTERNET_HDR(packet, pkt.hdr_len,
                              &routing_key, &dest_node, &dest_sock,
                              &src_node_or, &src_node, &src_sock,
                              info_out, &id_out,
                              packet_data, sizeof(packet_data),
                              &data_len, &status);

        /* 0x00E68AD2: D4 = the routing key, read again by the response arm */
        src_network = (int32_t)routing_key;
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

    /*
     * 0x00E68B2E-0x00E68B42: the netbuf page A3 was computed from the header
     * VA at 0x00E68A46; +0x3E0 is the receiving port's network number and
     * +0x3E2 its socket, zero-extended to a longword by "clr.l D5 /
     * move.w (0x3e2,A3),D5w".
     */
    port_network = *(uint16_t *)(page_base + 0x3E0);
    port_index = ROUTE_$FIND_PORT(port_network,
                                  (uint32_t)*(uint16_t *)(page_base + 0x3E2));

    /* 0x00E68B46-0x00E68B54: the header buffer goes back either way */
    hdr_va = (uint32_t)(uintptr_t)packet;
    NETBUF_$RTN_HDR(&hdr_va);

    if (port_index == -1) {
        /* Unknown port - ignore packet */
        return;
    }

    /* Dispatch based on command type */
    uint16_t command = packet_data[0];

    switch (command) {
    case RIP_CMD_REQUEST:
        /*
         * RIP Request - send back routing information
         */
        if (is_xns < 0) {
            /* Non-standard request */
            if (ROUTE_$STD_N_ROUTING_PORTS < 2) {
                /* Check if this is a broadcast request (all FFs in address) */
                uint16_t *addr = (uint16_t *)&header_bytes[0x14];
                if (addr[0] == 0xFFFF && addr[1] == 0xFFFF && addr[2] == 0xFFFF) {
                    return;
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
                    xns_addr[i] = header_bytes[0x08 + i];  /* Source address in header */
                }
                *(uint16_t *)&xns_addr[10] = 1;  /* Socket 1? */

                for (i = 0; i < RIP_SEND_RETRIES; i++) {
                    RIP_$SEND(xns_addr, port_index, response_data,
                              RIP_$PACKET_LENGTH(response_count), 0xFF);

                    /* Wait for response or timeout */
                    /* TODO(source-6sz): TIME_$WAIT signature mismatch - needs further analysis */
                    uint16_t delay_type = 0;
                    clock_t delay = { 0, RIP_SEND_TIMEOUT };
                    status_$t wait_status;

                    TIME_$WAIT(&delay_type, &delay, &wait_status);

                    if (wait_status == 0xD0003) {
                        /* Timeout - done */
                        return;
                    }
                }
            }
            return;
        } else {
            /* Standard request */
            if (ROUTE_$N_ROUTING_PORTS < 2) {
                uint8_t *response_flags = (uint8_t *)&packet_data[0x155];
                if ((int8_t)*response_flags < 0) {
                    return;
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

            /*
             * Send the response (0x00E68C38-0x00E68C7E).  The pushes, in
             * argument order, are:
             *   1  (-0x4f4,A6)   2  (-0x4f8,A6)   3  (-0x51a,A6) word
             *   4  D4            5  NODE_$ME      6  #8 word
             *   7  &(-0x2b0,A6)  8  (-0x518,A6) word
             *   9  &(-0x290,A6)  10 RIP_$PACKET_LENGTH((-0x512,A6))
             *   11 pea (0x1de,PC) -> 0x00E68E28   12 #0 word
             *   13 &(-0x50e,A6)  14 &(-0x50c,A6)  15 &(-0x4ec,A6)
             * plus the 2-byte Pascal function-result slot at 0x00E68C38.
             *
             * PKT_$BLD_INTERNET_HDR writes a word through BOTH arguments 13
             * and 14 unconditionally (0x00E1230E, 0x00E12316), so neither may
             * be NULL; they are two distinct word locals here as in the
             * original.
             *
             * TODO(source-6sz): arguments 1-10 still need each A6
             * displacement matched to the local this translation names.
             */
            {
                uint16_t retry_hint;    /* A6-0x50E */
                uint16_t timeout_out;   /* A6-0x50C */

                PKT_$SEND_INTERNET(idp_network, idp_host, port_network,
                                   src_network, NODE_$ME, RIP_SOCKET,
                                   &response_cmd, port_socket,
                                   response_data, RIP_$PACKET_LENGTH(response_count),
                                   RIP_$ANNOUNCE_EXTRA, 0,
                                   &retry_hint, &timeout_out, &status);
            }
            return;
        }
        break;

    case RIP_CMD_RESPONSE:
        /*
         * RIP Response - update routing table
         */
        port = ROUTE_$PORTP[port_index];

        /* Get source network from packet or header */
        if (is_xns < 0) {
            src_network = *(int32_t *)&header_bytes[0x1A];
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
                                is_xns, &status);

                /* Add new network route */
                source_addr.network = src_network;
                RIP_$UPDATE_INT(src_network, &source_addr, 0, port_index,
                                is_xns, &status);

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
        if (is_xns < 0) {
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
        if (is_xns < 0) {
            /* Copy host from header for non-standard */
            source_addr.host[0] = header_bytes[0x0A];
            source_addr.host[1] = header_bytes[0x0B];
            source_addr.host[2] = header_bytes[0x0C];
            source_addr.host[3] = header_bytes[0x0D];
            source_addr.host[4] = header_bytes[0x0E];
            source_addr.host[5] = header_bytes[0x0F];
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

            RIP_$UPDATE_INT(net, &source_addr, metric, port_index, is_xns, &status);
        }

    send_updates:
        /* Send any pending updates */
        RIP_$SEND_UPDATES(is_xns);
        return;

    case RIP_CMD_NAME_REGISTER:
        /*
         * Name service registration (Apollo extension)
         *
         * The original pushes two arguments to REM_NAME_$REGISTER_SERVER,
         * but the routine (0xE4A4AE) ignores them; see name/name.h.
         */
        if (is_xns < 0) {
            /* Non-standard - check for specific socket type */
            if ((uint8_t)header_bytes[0x1B] != 0xBE) {
                RIP_$STATS.unknown_commands++;
                return;
            }

            /* Extract parameters - kept for documentation */
            /* uint32_t param1 = *(uint32_t *)&header_bytes[0x14]; */
            /* uint32_t param2 = *(uint32_t *)&header_bytes[0x04] & 0xFFFFF; */
            REM_NAME_$REGISTER_SERVER();
        } else {
            /* Standard - call name registration */
            REM_NAME_$REGISTER_SERVER();
        }
        return;

    default:
        /* Unknown command */
        RIP_$STATS.unknown_commands++;
        return;
    }

    return;

error_return:
    /* Error - return packet and increment error counter */
    RIP_$STATS.errors++;
    {
        uint32_t pkt_va = (uint32_t)(uintptr_t)packet;
        NETBUF_$RTN_HDR(&pkt_va);
    }
    return;
}
