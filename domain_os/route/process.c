/*
 * ROUTE_$PROCESS - Main routing process entry point
 *
 * This is the routing server process created by ROUTE_$INIT_ROUTING.
 * It runs as a separate process, handling:
 *   - Periodic RIP broadcasts (every ~114 ticks)
 *   - Forwarding incoming packets to appropriate destinations
 *   - Shutdown coordination
 *
 * The process uses EC_$WAIT to multiplex between:
 *   - TIME_$CLOCKH: Timer for periodic broadcasts
 *   - Socket EC: Incoming packets on routing socket
 *   - Control EC: Shutdown signal
 *
 * Original address: 0x00E873EC
 */

#include "route/route_internal.h"
#include "ec/ec.h"
#include "proc1/proc1.h"
#include "sock/sock.h"
#include "rip/rip.h"
#include "rip/rip_internal.h"
#include "time/time.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "net_io/net_io.h"
#include "network/network.h"
#include "mac_os/mac_os.h"
#include "xns_idp/xns_idp.h"
#include "wp/wp.h"
#include "ring/ringlog.h"

/* Timer interval for RIP broadcasts (0x72 = 114 ticks) */
#define RIP_BROADCAST_INTERVAL  0x72

/* Maximum packet size for forwarding (1024 bytes) */
#define MAX_FORWARD_SIZE        0x400

/* Maximum hop count (from XNS IDP spec) */
#define MAX_HOP_COUNT           0x10

/* Ethernet type for IP (0x600) */
#define ETHER_TYPE_IP           0x600

/* Lock ID for routing operations */
#define ROUTE_LOCK_ID           0x0D
#define NET_IO_LOCK_ID          0x18

/* Global data references */
#if defined(M68K)
#define ROUTE_$START_TIME       (*(uint32_t *)0xE825DC)
#define ROUTE_$PROCESS_UID      (*(uint16_t *)0xE88216)
#define ROUTE_$USER_PORT_MAX    (*(uint16_t *)0xE87FD0)
#define ROUTE_$WIRED_COUNT      (*(int16_t *)0xE87FD2)
#define ROUTE_$USER_PORT_COUNT_W (*(int16_t *)0xE87FD4)
#define ROUTE_$WIRED_PAGES      ((uint32_t *)0xE87D80)
#define ROUTE_$PACKET_STATS     ((uint32_t *)0xE87DA8)
#define ROUTE_$SERVICE_ID       (*(uint16_t *)0xE8821C)
#define ROUTE_$NET_SERVICE_ON   (*(uint16_t *)0xE8789C)
#define ROUTE_$NET_SERVICE_OFF  (*(uint16_t *)0xE8789E)
#define ROUTE_$FWD_TIMEOUT      (*(uint16_t *)0xE88224)
#define NODE_$ME                (*(uint32_t *)0xE245A4)
#define RING_$LOGGING_NOW       (*(int8_t *)0xE2C364)
#define PTR_ROUTE_$CONTROL_EC   (*(ec_$eventcount_t **)0xE88220)

/* Statistics counters */
#define STAT_OVERSIZED_STD      (*(uint32_t *)0xE87FAC)
#define STAT_OVERSIZED_N        (*(uint32_t *)0xE87FBC)
#define STAT_DROPPED_STD_HOP    (*(uint32_t *)0xE87FB0)
#define STAT_DROPPED_N_HOP      (*(uint32_t *)0xE87FC0)
#define STAT_DROPPED_STD_ROUTE  (*(uint32_t *)0xE87FB4)
#define STAT_DROPPED_N_ROUTE    (*(uint32_t *)0xE87FC4)
#define STAT_FORWARDED_STD      (*(uint32_t *)0xE87FB8)
#define STAT_FORWARDED_N        (*(uint32_t *)0xE87FC8)

#else
extern uint32_t ROUTE_$START_TIME;
extern uint16_t ROUTE_$PROCESS_UID;
extern uint16_t ROUTE_$USER_PORT_MAX;
extern int16_t ROUTE_$WIRED_COUNT;
extern int16_t ROUTE_$USER_PORT_COUNT_W;
extern uint32_t ROUTE_$WIRED_PAGES[];
extern uint32_t ROUTE_$PACKET_STATS[];
extern uint16_t ROUTE_$SERVICE_ID;
extern uint16_t ROUTE_$NET_SERVICE_ON;
extern uint16_t ROUTE_$NET_SERVICE_OFF;
extern uint16_t ROUTE_$FWD_TIMEOUT;
extern uint32_t NODE_$ME;
extern int8_t RING_$LOGGING_NOW;
extern ec_$eventcount_t *PTR_ROUTE_$CONTROL_EC;

extern uint32_t STAT_OVERSIZED_STD;
extern uint32_t STAT_OVERSIZED_N;
extern uint32_t STAT_DROPPED_STD_HOP;
extern uint32_t STAT_DROPPED_N_HOP;
extern uint32_t STAT_DROPPED_STD_ROUTE;
extern uint32_t STAT_DROPPED_N_ROUTE;
extern uint32_t STAT_FORWARDED_STD;
extern uint32_t STAT_FORWARDED_N;
#endif

/* IDP packet header structure (XNS Internet Datagram Protocol) */
typedef struct {
    uint16_t    checksum;       /* 0x00: Checksum (0xFFFF = no checksum) */
    uint16_t    length;         /* 0x02: Total packet length */
    uint8_t     transport_ctl;  /* 0x04: Transport control (hop count in low 4 bits) */
    uint8_t     packet_type;    /* 0x05: Packet type */
    /* Destination address */
    uint32_t    dst_network;    /* 0x06: Destination network */
    uint16_t    dst_host_hi;    /* 0x0A: Destination host (high) */
    uint32_t    dst_host_lo;    /* 0x0C: Destination host (low) */
    uint16_t    dst_socket;     /* 0x10: Destination socket */
    /* Source address */
    uint32_t    src_network;    /* 0x12: Source network */
    uint16_t    src_host_hi;    /* 0x16: Source host (high) */
    uint32_t    src_host_lo;    /* 0x18: Source host (low) */
    uint16_t    src_socket;     /* 0x1C: Source socket */
    /* Data follows */
} idp_header_t;

/* Error status for SOCK_$GET failure */
static const status_$t status_$route_sock_get_failed = 0x2B00C6;

/* Ring log message identifier */
#if defined(M68K)
#define RINGLOG_$ROUTE_FORWARD  (*(uint16_t *)0xE878A0)
#else
extern uint16_t RINGLOG_$ROUTE_FORWARD;
#endif

/*
 * Note: All external function declarations come from the included headers:
 *   - NETWORK_$SET_SERVICE from network/network.h
 *   - WP_$UNWIRE from wp/wp.h
 *   - PROC1_$UNBIND, PROC1_$SET_LOCK, PROC1_$CLR_LOCK from proc1/proc1.h
 *   - RIP_$FIND_NEXTHOP from rip/rip.h
 *   - RIP_$BROADCAST from rip/rip_internal.h
 *   - NETBUF_$RTN_HDR from netbuf/netbuf.h
 *   - PKT_$DUMP_DATA from pkt/pkt.h
 *   - RINGLOG_$LOGIT from ring/ringlog.h
 *   - MAC_OS_$ARP, MAC_OS_$SEND from mac_os/mac_os.h
 *   - NET_IO_$SEND from net_io/net_io.h
 *   - ML_$LOCK, ML_$UNLOCK from ml/ml.h (via xns_idp.h)
 */

void ROUTE_$PROCESS(void)
{
    ec_$eventcount_t *ecs[3];
    uint32_t ec_vals[3];
    int16_t wait_result;
    ec_$eventcount_t *socket_ec;
    uint32_t *next_broadcast_time;
    status_$t status;
    uint32_t *packet_buffer;
    void *packet_ptr;
    idp_header_t *idp_hdr;
    int16_t next_hop_port;
    uint8_t next_hop_addr[6];
    uint8_t is_std_routing;     /* 0xFF if standard routing, 0 if normal */
    uint8_t should_forward;     /* 0xFF if packet should be forwarded */
    uint8_t was_forwarded;      /* 0xFF if successfully forwarded */
    uint8_t hop_count;
    int16_t i;
    route_$port_t *dest_port;
    int32_t port_index;
    uint16_t packet_size;
    uint32_t packet_network;

    /*
     * Wait for initialization to complete
     * ROUTE_$INIT_ROUTING advances CONTROL_EC when ready
     */
    EC_$WAITN(&PTR_ROUTE_$CONTROL_EC, &ROUTE_$CONTROL_ECVAL, 1);
    ROUTE_$CONTROL_ECVAL++;

    /*
     * Get socket event counter for our routing socket
     */
    socket_ec = SOCK_$EVENT_COUNTERS[ROUTE_$SOCK];

    /*
     * Mark routing as active
     */
    ROUTE_$ROUTING = 0xFF;

    /*
     * Register network service (enables routing in network stack)
     */
    NETWORK_$SET_SERVICE(&ROUTE_$NET_SERVICE_ON, &ROUTE_$SERVICE_ID, &status);

    /*
     * Initialize broadcast timer
     */
    next_broadcast_time = (uint32_t *)TIME_$CLOCKH;

    /*
     * Lock routing operations
     */
    PROC1_$SET_LOCK(ROUTE_LOCK_ID);

    /*
     * Main routing loop
     */
    for (;;) {
        /*
         * Set up event counters for multiplexed wait:
         *   [0] = TIME_$CLOCKH (for periodic broadcasts)
         *   [1] = Socket EC (for incoming packets)
         *   [2] = Control EC (for shutdown)
         */
        ecs[0] = (ec_$eventcount_t *)&TIME_$CLOCKH;
        ecs[1] = socket_ec;
        ecs[2] = (ec_$eventcount_t *)&ROUTE_$CONTROL_EC;

        ec_vals[0] = (uint32_t)next_broadcast_time;
        ec_vals[1] = ROUTE_$SOCK_ECVAL;
        ec_vals[2] = ROUTE_$CONTROL_ECVAL;

        /*
         * Wait for any event
         */
        wait_result = EC_$WAIT(ecs, ec_vals);

        switch (wait_result) {
        case 0:
            /*
             * Timer expired - broadcast RIP updates
             */
            if (ROUTE_$N_ROUTING_PORTS > 1) {
                RIP_$BROADCAST(0x00);   /* Normal routing */
            }
            if (ROUTE_$STD_N_ROUTING_PORTS > 1) {
                RIP_$BROADCAST(0xFF);   /* Standard routing */
            }

            /* Schedule next broadcast */
            next_broadcast_time = (uint32_t *)((uint32_t)TIME_$CLOCKH + RIP_BROADCAST_INTERVAL);
            break;

        case 1:
            /*
             * Packet received on routing socket
             */
            if (SOCK_$GET(ROUTE_$SOCK, &packet_ptr) >= 0) {
                /* SOCK_$GET should return negative on success */
                CRASH_SYSTEM(&status_$route_sock_get_failed);
            }

            packet_buffer = (uint32_t *)packet_ptr;

            /*
             * Update packet size statistics
             * Index is capped at 0x80 (128)
             */
            {
                uint8_t size_index = ((uint8_t *)socket_ec)[0x15];
                if (size_index > 0x80) {
                    size_index = 0x80;
                }
                ROUTE_$PACKET_STATS[size_index]++;
            }

            /*
             * Determine routing type from packet flags
             * Bit 1 of flags at offset -0x9F indicates standard routing
             */
            is_std_routing = 0;  /* Will be set based on packet flags */

            /*
             * Get IDP header pointer
             * If standard routing, header is at packet start
             * Otherwise, header is at packet + 0x28 (after MAC header)
             */
            if (is_std_routing) {
                idp_hdr = (idp_header_t *)packet_buffer;
            } else {
                idp_hdr = (idp_header_t *)((uint8_t *)packet_buffer + 0x28);
            }

            should_forward = 0xFF;

            /*
             * Check for broadcast with non-zero source socket
             * (should not be forwarded)
             */
            /* Complex condition from original - simplified */

            /*
             * Increment transport control (hop count)
             */
            idp_hdr->transport_ctl++;

            /*
             * Recalculate checksum if not disabled
             */
            if (idp_hdr->checksum != 0xFFFF) {
                idp_hdr->checksum = XNS_IDP_$HOP_AND_SUM(idp_hdr->checksum, idp_hdr->length);
            }

            /*
             * Check hop count limit
             */
            if (should_forward && idp_hdr->transport_ctl > MAX_HOP_COUNT) {
                if (is_std_routing) {
                    STAT_DROPPED_STD_HOP++;
                } else {
                    STAT_DROPPED_N_HOP++;
                }
                should_forward = 0;
            }

            /*
             * Find next hop for destination
             */
            if (should_forward) {
                RIP_$FIND_NEXTHOP(&idp_hdr->dst_network, 0, &next_hop_port,
                                  next_hop_addr, &status);

                if (status != status_$ok) {
                    if (is_std_routing) {
                        STAT_DROPPED_STD_ROUTE++;
                    } else {
                        STAT_DROPPED_N_ROUTE++;
                    }
                    should_forward = 0;
                }
            }

            was_forwarded = 0;

            if (should_forward) {
                port_index = (int32_t)next_hop_port;
                dest_port = &ROUTE_$PORT_ARRAY[port_index];

                /*
                 * Validate destination port is in appropriate routing mode
                 */
                if (is_std_routing) {
                    if (((1 << (dest_port->active & 0x1f)) & 0x30) == 0) {
                        STAT_DROPPED_STD_ROUTE++;
                        should_forward = 0;
                    }
                } else {
                    if (((1 << (dest_port->active & 0x1f)) & 0x28) == 0) {
                        STAT_DROPPED_N_ROUTE++;
                        should_forward = 0;
                    }

                    /* Set source node and network for outgoing */
                    *(uint32_t *)((uint8_t *)packet_buffer + 8) = NODE_$ME;
                    *packet_buffer = packet_network & 0xFFFFF;
                }

                /*
                 * Forward based on destination port type
                 */
                if (dest_port->port_type == ROUTE_PORT_TYPE_ROUTING) {
                    /*
                     * Type 2: User/routing port - send via socket
                     */
                    if (RING_$LOGGING_NOW < 0) {
                        RINGLOG_$LOGIT(&RINGLOG_$ROUTE_FORWARD, packet_buffer);
                    }

                    if (SOCK_$PUT(dest_port->socket, &packet_ptr, 0, 2,
                                  dest_port->socket) < 0) {
                        was_forwarded = 0xFF;
                        /* Update port-specific statistics */
                    } else {
                        /* Update error statistics */
                    }

                    /* Update port forward counter */
                    *(uint32_t *)((uint8_t *)dest_port + 0x58) += 1;

                } else if (packet_size <= MAX_FORWARD_SIZE) {
                    /*
                     * Type 1: Local network port
                     */
                    if (is_std_routing) {
                        /*
                         * Standard routing - use MAC_OS_$SEND
                         */
                        uint8_t hw_addr[6];
                        uint8_t arp_info[4];

                        MAC_OS_$ARP(next_hop_addr, next_hop_port, hw_addr, arp_info, &status);

                        if (status == status_$ok) {
                            /* Build and send ethernet frame */
                            /* ... complex packet construction ... */
                            MAC_OS_$SEND(NULL, hw_addr, NULL, &status);
                        }
                    } else {
                        /*
                         * Normal routing - use NET_IO_$SEND
                         */
                        void *pkt_array[1];
                        uint32_t flags;

                        /* Get flags from packet buffer page */
                        flags = *(uint32_t *)(((uint32_t)packet_buffer & ~0x3FF) + 0x3FC);

                        ML_$LOCK(NET_IO_LOCK_ID);

                        pkt_array[0] = packet_buffer;
                        NET_IO_$SEND(next_hop_port, pkt_array, flags,
                                     *(int16_t *)((uint8_t *)packet_buffer + 0x10), 0,
                                     NULL,
                                     *(int16_t *)((uint8_t *)packet_buffer + 0x14),
                                     ROUTE_$FWD_TIMEOUT, NULL, &status);

                        ML_$UNLOCK(NET_IO_LOCK_ID);
                    }
                } else {
                    /* Packet too large to forward */
                    if (is_std_routing) {
                        STAT_OVERSIZED_STD++;
                    } else {
                        STAT_OVERSIZED_N++;
                    }
                }

                /* Update forwarding statistics */
                if (should_forward) {
                    if (is_std_routing) {
                        STAT_FORWARDED_STD++;
                    } else {
                        STAT_FORWARDED_N++;
                    }
                }
            }

            /*
             * Return packet buffer if not successfully forwarded
             */
            if (!was_forwarded) {
                NETBUF_$RTN_HDR(&packet_ptr);
                PKT_$DUMP_DATA(NULL, packet_size);
            }

            /* Advance socket EC for next packet */
            ROUTE_$SOCK_ECVAL++;
            break;

        case 2:
            /*
             * Shutdown requested via control EC
             */
            ROUTE_$CONTROL_ECVAL++;

            /* Release routing lock */
            PROC1_$CLR_LOCK(ROUTE_LOCK_ID);

            /* Mark routing as inactive */
            ROUTE_$ROUTING = 0;
            ROUTE_$START_TIME = 0;

            /* Unregister network service */
            NETWORK_$SET_SERVICE(&ROUTE_$NET_SERVICE_OFF, &ROUTE_$SERVICE_ID, &status);

            /* Close routing socket */
            {
                int16_t sock = ROUTE_$SOCK;
                ROUTE_$SOCK = 0xFFFF;
                SOCK_$CLOSE(sock);
            }

            ROUTE_$USER_PORT_MAX = 0;

            /*
             * Unwire wired pages if no user ports remain
             */
            if (ROUTE_$USER_PORT_COUNT_W == 0) {
                for (i = ROUTE_$WIRED_COUNT - 1; i >= 0; i--) {
                    WP_$UNWIRE(ROUTE_$WIRED_PAGES[i]);
                }
                ROUTE_$WIRED_COUNT = 0;
            }

            /* Unbind this process */
            PROC1_$UNBIND(ROUTE_$PROCESS_UID, &status);

            return;
        }
    }
}
