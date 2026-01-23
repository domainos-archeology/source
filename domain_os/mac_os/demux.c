/*
 * MAC_OS_$DEMUX - Demultiplex a received packet
 *
 * Routes an incoming packet to the appropriate channel callback
 * based on the packet type and port's packet type table.
 *
 * Original address: 0x00E0B816
 * Original size: 168 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$DEMUX
 *
 * This function is called by the network driver when a packet is received.
 * It looks up the packet type in the port's table and calls the appropriate
 * channel's callback function.
 *
 * Parameters:
 *   pkt_info   - Received packet information structure
 *                0x30: Packet type (32-bit)
 *                0x2A: Timestamp high (set by this function)
 *                0x2E: Timestamp low (set by this function)
 *                0x34: Channel pointer (set by this function)
 *   port_num   - Pointer to port number (0-7)
 *   param3     - Additional parameter (passed through to callback)
 *   status_ret - Pointer to receive status code
 *
 * Assembly notes:
 *   - Uses A5 = 0xE22990 (MAC_OS_$DATA base)
 *   - Gets current time via TIME_$ABS_CLOCK
 *   - Looks up packet type in port's table
 *   - Calls channel callback with: pkt_info, port_num, param3, status_ret
 */
void MAC_OS_$DEMUX(void *pkt_info, int16_t *port_num, void *param3, status_$t *status_ret)
{
#if defined(M68K)
    int16_t port;
    int16_t entry_idx;
    int16_t channel;
    mac_os_$port_pkt_table_t *port_table;
    mac_os_$channel_t *chan;
    uint32_t pkt_type;
    clock_t timestamp;

    *status_ret = status_$ok;

    /* Get current timestamp */
    TIME_$ABS_CLOCK(&timestamp);

    /* Get port's packet type table */
    port = *port_num;
    port_table = &((mac_os_$port_pkt_table_t *)MAC_OS_$DATA_BASE)[port];

    /* Get packet type from pkt_info (offset 0x30) */
    pkt_type = *(uint32_t *)((uint8_t *)pkt_info + 0x30);

    /* Search for matching packet type entry */
    entry_idx = MAC_OS_$FIND_PACKET_TYPE(pkt_type, &port_table->entries[0], port_table->entry_count);

    if (entry_idx == -1) {
        /* No matching entry found */
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /* Get channel index from entry */
    channel = port_table->entries[entry_idx].channel_index;
    chan = &MAC_OS_$CHANNEL_TABLE[channel];

    /* Check if channel has a callback */
    if (chan->callback == NULL) {
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /* Store timestamp in packet info */
    /* Original: move.l (-0x4c,A6),(0x2a,A4) - timestamp high */
    /* Original: move.w (-0x48,A6),(0x2e,A4) - timestamp low */
    *(uint32_t *)((uint8_t *)pkt_info + 0x2A) = timestamp.high;
    *(uint16_t *)((uint8_t *)pkt_info + 0x2E) = timestamp.low;

    /* Store pointer to channel in packet info (for receive processing) */
    *(void **)((uint8_t *)pkt_info + 0x34) = chan;

    /* Call channel callback */
    {
        void (*callback)(void *, int16_t *, void *, status_$t *);
        callback = chan->callback;
        (*callback)(pkt_info, port_num, param3, status_ret);
    }
#else
    /* Non-M68K implementation stub */
    (void)pkt_info;
    (void)port_num;
    (void)param3;
    *status_ret = status_$mac_XXX_unknown;
#endif
}
