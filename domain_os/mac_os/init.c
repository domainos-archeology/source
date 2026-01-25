/*
 * MAC_OS_$INIT - Initialize the MAC_OS subsystem
 *
 * Initializes the exclusion lock, clears all packet type tables,
 * initializes channel table, and sets up initial port configurations.
 *
 * Original address: 0x00E2F4FC
 * Original size: 306 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$INIT
 *
 * Initializes the MAC_OS subsystem:
 * 1. Initialize the exclusion lock
 * 2. For each port (0-7):
 *    - Initialize port info pointer
 *    - Clear packet type count
 *    - Initialize channel callback pointer
 *    - If port has a route port with driver info, set up ARP entry
 * 3. For each channel (0-9):
 *    - Clear in-use and open flags
 *    - Set socket to NO_SOCKET (0xE1)
 *    - Clear callback and driver_info pointers
 *
 * Assembly notes:
 *   - Uses A5 as base pointer to MAC_OS_$DATA (0xE22990)
 *   - First loop (D2=7, 8 iterations) handles ports
 *   - Second loop (D0=9, 10 iterations) handles channels
 *   - ROUTE_$PORTP is at 0xE26EE8 (array of 8 pointers)
 *   - NODE_$ME is at 0xE245A4 (local node address)
 */
void MAC_OS_$INIT(void)
{
    int16_t port;
    int16_t channel;
    mac_os_$port_pkt_table_t *port_table;
    mac_os_$channel_t *chan;
    void **route_portp;
    void *route_port;
    void *driver_info;
    uint32_t node_me;
    uint16_t node_lo;

    /* Initialize the exclusion lock */
    ML_$EXCLUSION_INIT((ml_$exclusion_t *)MAC_OS_$EXCLUSION);

    /* Get the route port pointer array */
    route_portp = (void **)0xE26EE8;

    /* Initialize each port's packet type table and channel callback */
    for (port = 0; port < MAC_OS_MAX_PORTS; port++) {
        port_table = &MAC_OS_$PORT_PKT_TABLES[port];
        chan = &MAC_OS_$CHANNEL_TABLE[port];

        /* Set up callback chain pointer (points to next entry's callback area) */
        /* Original: *(puVar5 + 0x43e) = puVar4 + 0x44e */
        /* This creates a linked list of callback slots */
        chan->callback = (void *)((uint32_t)&MAC_OS_$PORT_INFO_TABLE[port]);

        /* Clear packet type count */
        port_table->entry_count = 0;

        /* Initialize channel entry with default values */
        /* Original: move.l #0x1,(0x89c,A1) */
        MAC_OS_$PORT_INFO_TABLE[port].version = 1;
        MAC_OS_$PORT_INFO_TABLE[port].config = 0;

        /* Check if this port has a route port configured */
        route_port = route_portp[port];
        if (route_port == NULL) {
            continue;
        }

        /* Check if route port has driver info */
        driver_info = *(void **)((uint8_t *)route_port + ROUTE_PORT_DRIVER_INFO_OFFSET);
        if (driver_info == NULL) {
            continue;
        }

        /* Store MTU from driver info */
        /* Original: move.w (0x4,A0),(0x8a2,A1) */
        /* MAC_OS_$PORT_INFO_TABLE[port].mtu = *(uint16_t *)((uint8_t *)driver_info + 4); */

        /* Call the NOP placeholder function */
        MAC_OS_$NOP();

        /* Set up ARP table entry for this port */
        /* Initialize with local node address for IP/ARP resolution */
        /* Original code sets up an ARP entry structure at route_port */
        {
            uint32_t *arp_entry = (uint32_t *)route_port;

            /* Set version/type */
            arp_entry[1] = 0x10001;  /* Version 1, type 1 */
            *(uint16_t *)((uint8_t *)route_port + 8) = 2;

            /* Extract node address components */
            node_me = *(uint32_t *)0xE245A4;
            node_lo = (uint16_t)((node_me >> 16) & 0xF);

            /* Store node address for ARP */
            *(uint16_t *)((uint8_t *)route_port + 10) = node_lo;
            *(uint16_t *)((uint8_t *)route_port + 12) = *(uint16_t *)0xE245A6;

            /* Set up default ARP entry */
            arp_entry[8] = arp_entry[0];
            *(uint16_t *)((uint8_t *)route_port + 0x24) = 0x0800;  /* IP type */
            *(uint16_t *)((uint8_t *)route_port + 0x26) = node_lo | 0x1E00;
            *(uint16_t *)((uint8_t *)route_port + 0x28) = *(uint16_t *)((uint8_t *)route_port + 12);
            *(uint16_t *)((uint8_t *)route_port + 0x2A) = 0xFFFF;  /* Broadcast marker */
        }
    }

    /* Initialize each channel entry */
    for (channel = 0; channel < MAC_OS_MAX_CHANNELS; channel++) {
        chan = &MAC_OS_$CHANNEL_TABLE[channel];

        /* Clear the in-use flag (bit 0 of flags) */
        chan->flags &= ~MAC_OS_FLAG_PROMISCUOUS;

        /* Clear the open flag (bit 1 of flags) */
        chan->flags &= ~MAC_OS_FLAG_OPEN;

        /* Set socket to "no socket" value */
        chan->socket = MAC_OS_NO_SOCKET;

        /* Clear line number */
        chan->line_number = 0;

        /* Clear driver info and callback pointers */
        chan->driver_info = NULL;
        chan->callback = NULL;
    }
}
