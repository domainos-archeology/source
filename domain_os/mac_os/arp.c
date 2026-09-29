/*
 * MAC_OS_$ARP - Resolve address using ARP
 *
 * Resolves a network address to a MAC address. Handles broadcast
 * addresses specially.
 *
 * Original address: 0x00E0C0CE
 * Original size: 156 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$ARP
 *
 * This function performs address resolution for outgoing packets.
 * For broadcast addresses (all 0xFFFF), it fills in the broadcast
 * MAC address. For unicast addresses, it uses the network type to
 * determine how to construct the MAC address.
 *
 * Parameters:
 *   addr_info  - Address record; the three words this routine reads are the
 *                rip_$dest_addr_t host address:
 *                0x04: host_hi   ("cmpi.w #0x800,(0x4,A1)" 0x00E0C18E)
 *                0x06: high half of host_lo ("move.w (0x6,A1),D0w" 0x00E0C196)
 *                0x08: low half of host_lo  ("move.w (0x8,A1),(0x4,A2)")
 *   port_num   - Port number (0-7)
 *   mac_addr   - the mac_os_$link_addr_t to fill in
 *   flags      - Pointer to receive flags:
 *                0x00 = unicast
 *                0xFF = broadcast/multicast
 *   status_ret - Pointer to receive status code
 *
 * Assembly notes:
 *   - Uses switch on network type from route_port+0x2E
 *   - Ethernet/type 3: 2-byte address length
 *   - Token ring/FDDI: 3-byte address length with special prefix
 */
void MAC_OS_$ARP(void *addr_info, int16_t port_num, uint16_t *mac_addr,
                 uint8_t *flags, status_$t *status_ret)
{
    route_$port_t       *route_port;
    mac_os_$link_addr_t *link_addr = (mac_os_$link_addr_t *)mac_addr;
    const uint8_t       *addr = (const uint8_t *)addr_info;
    uint16_t             net_type;
    uint16_t             host_hi;
    uint16_t             host_lo_hi;
    uint16_t             host_lo_lo;
    int                  i;

    /* 0x00E0C0EA: clr.l (A0) */
    *status_ret = status_$ok;

    /* 0x00E0C0EE-0x00E0C10C: ROUTE_$PORTP[port_num] */
    route_port = ROUTE_$WIRED_DATA.portp[port_num];
    if (route_port == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* 0x00E0C110: clr.b (A3) */
    *flags = 0;

    host_hi    = *(const uint16_t *)(addr + 0x04);
    host_lo_hi = *(const uint16_t *)(addr + 0x06);
    host_lo_lo = *(const uint16_t *)(addr + 0x08);

    net_type = route_port->port_type;

    /*
     * 0x00E0C114-0x00E0C128: the broadcast test compares the three words in
     * the order +0x08, +0x04, +0x06 against 0xFFFF.
     */
    if (host_lo_lo == 0xFFFF && host_hi == 0xFFFF && host_lo_hi == 0xFFFF) {
        /* 0x00E0C12A: st (A3) */
        *flags = 0xFF;

        /*
         * The jump table at 0x00E0C142 (targets are that address plus the
         * table word): 0 and 3 -> 0x00E0C14E, 4 and 5 -> 0x00E0C156,
         * 1 and 2 -> 0x00E0C216, and "cmpi.w #0x6 / bcc" sends anything from
         * 6 upward to 0x00E0C216 as well.
         */
        switch (net_type) {
        case MAC_OS_NET_TYPE_ETHERNET:
        case MAC_OS_NET_TYPE_3:
            /* 0x00E0C14E: the count alone; no address words are written */
            link_addr->n_words = 2;
            return;

        case MAC_OS_NET_TYPE_TOKEN_RING:
        case MAC_OS_NET_TYPE_FDDI:
            /* 0x00E0C156-0x00E0C164: count 3 and three words of 0xFFFF */
            link_addr->n_words = 3;
            for (i = 0; i < 3; i++) {
                link_addr->addr[i] = 0xFFFF;
            }
            return;

        default:
            /* 0x00E0C216 */
            *status_ret = status_$mac_port_op_not_implemented;
            return;
        }
    }

    /*
     * The unicast jump table at 0x00E0C182: 0 and 3 -> 0x00E0C18E,
     * 4 -> 0x00E0C1FC, 5 -> 0x00E0C1C4, 1 and 2 (and >= 6) -> 0x00E0C216.
     */
    switch (net_type) {
    case MAC_OS_NET_TYPE_ETHERNET:
    case MAC_OS_NET_TYPE_3:
        /*
         * 0x00E0C18E-0x00E0C1C2: only an Apollo 08-00-1E-0n-nn-nn host
         * address resolves; the node id is the low nibble of host_lo's high
         * half plus its low half.
         */
        if (host_hi == MAC_OS_ETHERTYPE_IP && (host_lo_hi & 0xFF00) == 0x1E00) {
            link_addr->n_words = 2;
            link_addr->addr[0] = (uint16_t)(host_lo_hi & 0x000F);
            link_addr->addr[1] = host_lo_lo;
            return;
        }
        /* 0x00E0C1BA */
        *status_ret = status_$mac_arp_address_not_found;
        return;

    case MAC_OS_NET_TYPE_TOKEN_RING:
        /* 0x00E0C1FC: sets the count and falls into the shared copy below */
        link_addr->n_words = 3;
        break;

    case MAC_OS_NET_TYPE_FDDI:
        /* 0x00E0C1C4 */
        link_addr->n_words = 3;
        if (host_hi == MAC_OS_ETHERTYPE_IP && (host_lo_hi & 0xFF00) == 0x1E00) {
            /* 0x00E0C1DE-0x00E0C1FA */
            link_addr->addr[0] = 0x5000;
            link_addr->addr[1] = (uint16_t)((host_lo_hi & 0x00FF) | 0x7800);
            link_addr->addr[2] = host_lo_lo;
            return;
        }
        /* 0x00E0C1DC: otherwise it branches into the shared copy at 0xE0C200 */
        break;

    default:
        /* 0x00E0C216 */
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /*
     * 0x00E0C200-0x00E0C214: three words copied straight across, from
     * addr_info + 0x04 to link_addr->addr[0].
     */
    for (i = 0; i < 3; i++) {
        link_addr->addr[i] = *(const uint16_t *)(addr + 0x04 + 2 * i);
    }
}
