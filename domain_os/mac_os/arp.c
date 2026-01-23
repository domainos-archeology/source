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
 *   addr_info  - Address information structure:
 *                0x04: Ethernet type (2 bytes)
 *                0x06: High word of address (2 bytes)
 *                0x08: Low word of address (2 bytes)
 *   port_num   - Port number (0-7)
 *   mac_addr   - Pointer to receive MAC address (3 uint16_t values)
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
#if defined(M68K)
    void *route_port;
    uint16_t net_type;
    uint16_t ether_type;
    uint16_t addr_high;
    uint16_t addr_low;

    *status_ret = status_$ok;

    /* Get route port structure */
    route_port = ((void **)0xE26EE8)[port_num];
    if (route_port == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* Initialize flags to unicast */
    *flags = 0;

    /* Get address fields */
    ether_type = *(uint16_t *)((uint8_t *)addr_info + 4);
    addr_high = *(uint16_t *)((uint8_t *)addr_info + 6);
    addr_low = *(uint16_t *)((uint8_t *)addr_info + 8);

    /* Check for broadcast address (all 0xFFFF) */
    if (addr_low == 0xFFFF && ether_type == 0xFFFF && addr_high == 0xFFFF) {
        /* Broadcast address */
        *flags = 0xFF;

        net_type = *(uint16_t *)((uint8_t *)route_port + ROUTE_PORT_NET_TYPE_OFFSET);
        switch (net_type) {
        case MAC_OS_NET_TYPE_ETHERNET:
        case MAC_OS_NET_TYPE_3:
            /* Ethernet broadcast: just 2 bytes */
            mac_addr[0] = 2;  /* Address length */
            break;

        case MAC_OS_NET_TYPE_TOKEN_RING:
        case MAC_OS_NET_TYPE_FDDI:
            /* Token ring/FDDI broadcast: 3 bytes, all 0xFFFF */
            mac_addr[0] = 3;  /* Address length */
            mac_addr[1] = 0xFFFF;
            mac_addr[2] = 0xFFFF;
            mac_addr[3] = 0xFFFF;
            break;

        default:
            *status_ret = status_$mac_port_op_not_implemented;
            break;
        }
        return;
    }

    /* Unicast address resolution */
    net_type = *(uint16_t *)((uint8_t *)route_port + ROUTE_PORT_NET_TYPE_OFFSET);

    switch (net_type) {
    case MAC_OS_NET_TYPE_ETHERNET:
    case MAC_OS_NET_TYPE_3:
        /* Ethernet: check for IP type (0x0800) with 0x1E00 prefix */
        if (ether_type == MAC_OS_ETHERTYPE_IP && (addr_high & 0xFF00) == 0x1E00) {
            mac_addr[0] = 2;  /* Address length */
            mac_addr[1] = addr_high & 0x000F;  /* Low nibble */
            mac_addr[2] = addr_low;
            return;
        }
        /* Non-IP or non-standard address */
        *status_ret = status_$mac_arp_address_not_found;
        return;

    case MAC_OS_NET_TYPE_TOKEN_RING:
        /* Token ring: 3-byte address */
        mac_addr[0] = 3;
        break;

    case MAC_OS_NET_TYPE_FDDI:
        /* FDDI: 3-byte address with special prefix */
        mac_addr[0] = 3;
        if (ether_type == MAC_OS_ETHERTYPE_IP && (addr_high & 0xFF00) == 0x1E00) {
            /* Convert IP-style address to FDDI format */
            mac_addr[1] = 0x5000;
            mac_addr[2] = (addr_high & 0xFF) | 0x7800;
            mac_addr[3] = addr_low;
            return;
        }
        break;

    default:
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* Copy address directly for non-IP types */
    mac_addr[1] = ether_type;
    mac_addr[2] = addr_high;
    mac_addr[3] = addr_low;
#else
    /* Non-M68K implementation stub */
    (void)addr_info;
    (void)port_num;
    (void)mac_addr;
    *flags = 0;
    *status_ret = status_$mac_port_op_not_implemented;
#endif
}
