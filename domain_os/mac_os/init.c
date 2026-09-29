/*
 * MAC_OS_$INIT - Initialise the MAC_OS subsystem
 *
 * Initialises the exclusion lock, clears every port's packet-type table,
 * publishes the port info records, seeds each configured port's own link and
 * XNS addresses from NODE_$ME, and resets all ten channel slots.
 *
 * Original address: 0x00E2F4FC, size 306 bytes (0x00E2F4FC-0x00E2F62D).
 * A5 is not used here; the module base 0x00E22990 is loaded as an immediate.
 */

#include "mac_os/mac_os_internal.h"
#include "route/route.h"    /* ROUTE_$PORTP, route_$port_t */
#include "node/node.h"      /* NODE_$ME (0x00E245A4) */

/*
 * The XNS host address MAC_OS_$INIT builds for each port:
 *
 *   0x00E2F5C4  move.w #0x800,(0x24,A1)    host_hi   = 0x0800
 *   0x00E2F5CA  ori.w  #0x1e00,D0w         }  host_lo = (0x1E00 | node_hi) << 16
 *   0x00E2F5CE  move.w D0w,(0x26,A1)       }            | node_lo
 *   0x00E2F5D2  move.w (0xc,A2),(0x28,A1)  }
 *   0x00E2F5D8  move.w #-0x1,(0x2a,A1)     socket    = 0xFFFF
 *
 * i.e. the Apollo 08-00-1E-0n-nn-nn address built out of this node's id.
 */
#define MAC_OS_XNS_HOST_HI          0x0800
#define MAC_OS_XNS_HOST_LO_PREFIX   0x1E00
#define MAC_OS_XNS_SOCKET_NIL       0xFFFF

void MAC_OS_$INIT(void)
{
    int16_t         port;
    int16_t         channel;
    route_$port_t  *route_port;
    void           *driver_info;
    uint16_t        node_hi;
    uint16_t        node_lo;

    /* 0x00E2F504-0x00E2F514: ML_$EXCLUSION_INIT(base + 0x868) */
    ML_$EXCLUSION_INIT(&MAC_OS_$EXCLUSION);

    /* 0x00E2F516-0x00E2F5F2: "moveq #0x7,D2" ... "dbf D2w" -> 8 ports */
    for (port = 0; port < MAC_OS_MAX_PORTS; port++) {
        /*
         * 0x00E2F54C: lea (0x89c,A0),A3 / move.l A3,(0x87c,A4)
         * A0 walks base + 8*port, A4 walks base + 4*port.
         */
        MAC_OS_$PORTP_TABLE[port] = &MAC_OS_$PORT_TABLE[port];

        /* 0x00E2F558: clr.w (A1), A1 walking base + 0xF4*port */
        MAC_OS_$PORT_PKT_TABLES[port].entry_count = 0;

        /* 0x00E2F55E / 0x00E2F566 */
        MAC_OS_$PORT_TABLE[port].version = 1;
        MAC_OS_$PORT_TABLE[port].config  = 0;

        /* 0x00E2F56A-0x00E2F572: ROUTE_$PORTP[port] */
        route_port = ROUTE_$WIRED_DATA.portp[port];
        if (route_port == NULL) {
            continue;
        }

        /* 0x00E2F578-0x00E2F580: route_port->driver_info */
        driver_info = (void *)ARCH_VA_TO_PTR(route_port->driver_info);
        if (driver_info == NULL) {
            continue;
        }

        /* 0x00E2F582: move.w (0x4,A0),(0x8a2,A1) */
        MAC_OS_$PORT_TABLE[port].mtu =
            *(uint16_t *)((uint8_t *)driver_info + MAC_OS_DRIVER_MTU_OFFSET);

        /* 0x00E2F588: jsr MAC_OS_$NOP */
        MAC_OS_$NOP();

        /*
         * 0x00E2F5A2-0x00E2F5B8:
         *   move.l (NODE_$ME),D0 / clr.w D0w / swap D0 / andi.l #0xf,D0
         * is the high word of NODE_$ME masked to four bits, and
         *   move.w (NODE_$ME+2),(0xc,A2)
         * is its low word.  Bit operations rather than byte reads so the two
         * halves come out the same on a little-endian host.
         */
        node_hi = (uint16_t)((NODE_$ME >> 16) & 0x000F);
        node_lo = (uint16_t)(NODE_$ME & 0xFFFF);

        /* 0x00E2F590: move.l #0x10001,(0x4,A0) */
        *(uint32_t *)((uint8_t *)route_port + ROUTE_PORT_LINK_ID_OFFSET) =
            0x00010001u;

        /*
         * 0x00E2F59C-0x00E2F5B8: the port's own two-word link address, the
         * same {count, words} record MAC_OS_$ARP builds.
         */
        {
            mac_os_$link_addr_t *link_addr = (mac_os_$link_addr_t *)
                ((uint8_t *)route_port + ROUTE_PORT_LINK_ADDR_OFFSET);

            link_addr->n_words = 2;
            link_addr->addr[0] = node_hi;
            link_addr->addr[1] = node_lo;
        }

        /*
         * 0x00E2F5C0-0x00E2F5D8: the port's own XNS endpoint.  host_lo is
         * written as the two words 0x26 and 0x28; one 32-bit store of the
         * same value lands the same bytes.
         */
        route_port->xns_addr.network = route_port->network;
        route_port->xns_addr.host_hi = MAC_OS_XNS_HOST_HI;
        route_port->xns_addr.host_lo =
            ((uint32_t)(uint16_t)(MAC_OS_XNS_HOST_LO_PREFIX | node_hi) << 16)
            | (uint32_t)node_lo;
        route_port->xns_addr.socket  = MAC_OS_XNS_SOCKET_NIL;
    }

    /* 0x00E2F5F6-0x00E2F620: "moveq #0x9,D0" ... "dbf D0w" -> 10 channels */
    for (channel = 0; channel < MAC_OS_MAX_CHANNELS; channel++) {
        mac_os_$channel_t *chan = &MAC_OS_$CHANNEL_TABLE[channel];

        /*
         * 0x00E2F5FE / 0x00E2F604: bclr.b #0x0 and #0x1 on the byte at
         * channel offset 0x12.  That byte is the flags word's HIGH half, so
         * these clear word bits 8 and 9 - MAC_OS_CHANNEL_PROMISCUOUS and
         * MAC_OS_CHANNEL_IN_USE - not bits 0 and 1 (bead source-b6p8).
         */
        chan->flags &= (uint16_t)~MAC_OS_CHANNEL_PROMISCUOUS;
        chan->flags &= (uint16_t)~MAC_OS_CHANNEL_IN_USE;

        /* 0x00E2F60A */
        chan->socket = MAC_OS_CHANNEL_NO_SOCKET;

        /* 0x00E2F610 */
        chan->line_number = 0;

        /* 0x00E2F614 / 0x00E2F618 */
        chan->driver_info = 0;
        chan->callback    = 0;
    }
}
