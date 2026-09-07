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
 *   pkt_info   - mac_os_$rcv_pkt_t the driver built; see mac_os/mac_os.h
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
void MAC_OS_$DEMUX(mac_os_$rcv_pkt_t *pkt_info, int16_t *port_num,
                   void *param3, status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t entry_idx;
    int16_t channel;
    mac_os_$port_pkt_table_t *port_table;
    mac_os_$channel_t *chan;
    clock_t timestamp;                  /* A6-0x4C */

    *status_ret = status_$ok;           /* 0x00E0B830 clr.l (A3) */

    /* 0x00E0B832 */
    TIME_$ABS_CLOCK(&timestamp);

    /*
     * 0x00E0B83E: the port index is a WORD read through the second argument,
     * and the table stride is 0xF4.
     */
    port_table = &MAC_OS_$PORT_PKT_TABLES[*port_num];

    /*
     * 0x00E0B84A: MAC_OS_$FIND_PACKET_TYPE(frame_type, &table->entries[0],
     * table->entry_count) - the pushes are the count word, the entry array
     * address and the frame type, so the frame type is argument 1.
     */
    entry_idx = MAC_OS_$FIND_PACKET_TYPE(pkt_info->frame_type,
                                         &port_table->entries[0],
                                         port_table->entry_count);

    /*
     * 0x00E0B85E: a miss, or a channel with no callback, is the same error.
     * The SR10.4 status-code table has no text for 0x003A000F, so the name
     * stays the placeholder mac_os.h already carries.
     */
    if (entry_idx == -1) {
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /* 0x00E0B864: entries are 12 bytes; the channel index is at +0x08 */
    channel = (int16_t)port_table->entries[entry_idx].channel_index;

    /* 0x00E0B870: channel entries are 0x14 bytes at MAC_OS_$CHANNEL_TABLE */
    chan = &MAC_OS_$CHANNEL_TABLE[channel];

    /* 0x00E0B882: tst.l (0x7a0,A2) - the callback is the entry's first field */
    if (chan->callback == NULL) {
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /* 0x00E0B890 / 0x00E0B896: the 48-bit arrival time, in its two halves */
    pkt_info->time_high = timestamp.high;
    pkt_info->time_low  = timestamp.low;

    /* 0x00E0B89C: the channel entry's address, not its index */
    pkt_info->channel = (uint32_t)(uintptr_t)chan;

    /* 0x00E0B8A4 - 0x00E0B8B2 */
    {
        void (*callback)(mac_os_$rcv_pkt_t *, int16_t *, void *, status_$t *);
        callback = (void (*)(mac_os_$rcv_pkt_t *, int16_t *, void *,
                             status_$t *))chan->callback;
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
